// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <codecvt>
#include <filesystem>
#include <functional>
#include <gsl/gsl>
#include "core/common/inlined_containers.h"
#include "core/framework/allocator.h"
#include "core/framework/config_options.h"
#include "core/framework/ort_value.h"
#include "core/session/onnxruntime_c_api.h"
#include "core/optimizer/graph_transformer_level.h"
#include "core/util/thread_utils.h"

#if !defined(ORT_MINIMAL_BUILD) || defined(ORT_MINIMAL_BUILD_CUSTOM_OPS)
#include "core/framework/library_handles.h"
#endif

namespace onnxruntime {

enum class ExecutionOrder {
  DEFAULT = 0,           // default topological sort
  PRIORITY_BASED = 1,    // priority-based topological sort
  MEMORY_EFFICIENT = 2,  // memory-efficient topological sort for training purposes.
};

inline std::ostream& operator<<(std::ostream& os, const ExecutionOrder& order) {
  switch (order) {
    case ExecutionOrder::DEFAULT:
      os << "DEFAULT";
      break;
    case ExecutionOrder::PRIORITY_BASED:
      os << "PRIORITY_BASED";
      break;
    case ExecutionOrder::MEMORY_EFFICIENT:
      os << "MEMORY_EFFICIENT";
      break;
    default:
      os << "UNKNOWN";
      break;
  }
  return os;
}

enum class FreeDimensionOverrideType {
  Invalid = 0,
  Denotation = 1,
  Name = 2
};

enum class ExecutionPriority : int {
  GLOBAL_HIGHT = -100,
  LOCAL_HIGH = -10,
  DEFAULT = 0,
  LOCAL_LOW = 10,
  GLOBAL_LOW = 100
};

struct FreeDimensionOverride {
  std::string dim_identifier;
  FreeDimensionOverrideType dim_identifier_type;
  int64_t dim_value;
};

using CheckLoadCancellationFn = std::function<bool()>;

/// <summary>
/// Options that configure the generation of a compiled model (i.e., a model with EPContext nodes).
/// There are two ways to compile a model:
///   1. By specifying the correct session option configurations and creating an inference session.
///      The compiled model is generated as a side-effect of session creation.
///   2. Using an explicit compile API (see OrtCompileApi struct in onnxruntime_c_api.h).
///
/// The default values in this struct are set to match the current/default behavior of approach 1 to maintain
/// compatibility with the older way of compiling. The explicit compile API overrides some of these values to
/// provide its own defaults (see core/session/model_compilation_options.h/cc).
/// </summary>
struct EpContextModelGenerationOptions {
  // Action to take if the output model does not have compiled (EPContext) nodes.
  enum class ActionIfNoCompiledNodes {
    // Return OK() but don't generate an output model. Compiling via SessionOptions defaults to this behavior
    // to maintain compatibility. The explicit compile API does *not* use this action.
    kDontGenerateModel = 0,

    // Generate an output model even if it doesn't have compiled nodes.
    // The explicit Compile API defaults to this value.
    kGenerateModel,

    // Return an error if the model does not have compiled nodes.
    // The explicit Compile API can be configured to this value.
    kReturnError,
  };

  EpContextModelGenerationOptions() = default;

  // Initializes from string key/value pairs in session config options.
  // This initializes this struct from options set via the older compiling approach #1 above.
  explicit EpContextModelGenerationOptions(const ConfigOptions& config_options);

  bool enable = false;
  bool error_if_output_file_exists = true;
  ActionIfNoCompiledNodes action_if_no_compiled_nodes = ActionIfNoCompiledNodes::kDontGenerateModel;
  bool embed_ep_context_in_model = false;

  std::string output_model_file_path;
  void** output_model_buffer_ptr = nullptr;
  size_t* output_model_buffer_size_ptr = nullptr;
  AllocatorPtr output_model_buffer_allocator = nullptr;

  std::string output_external_initializers_file_path;
  size_t output_external_initializer_size_threshold = 0;
};

struct EpSelectionPolicy {
  // flag to detect that a policy was set by the user.
  // need to preserve current behavior of defaulting to CPU EP if no EPs are explicitly registered
  // and no selection policy was explicitly provided.
  bool enable{false};
  OrtExecutionProviderDevicePolicy policy = OrtExecutionProviderDevicePolicy_DEFAULT;
  EpSelectionDelegate delegate{};
  void* state{nullptr};  // state for the delegate
};

/**
 * Configuration information for a session.
 */
struct SessionOptions {
#if defined(__wasm__) && defined(__EMSCRIPTEN_PTHREADS__)
  static constexpr bool DEFAULT_USE_PER_SESSION_THREADS = false;
#else
  static constexpr bool DEFAULT_USE_PER_SESSION_THREADS = true;
#endif
  ExecutionMode execution_mode = ExecutionMode::ORT_SEQUENTIAL;

  // set the execution order of the graph
  ExecutionOrder execution_order = ExecutionOrder::DEFAULT;

  // enable profiling for this session.
  bool enable_profiling = false;

  // Non empty filepath enables serialization of the transformed optimized model to the specified filepath.
  //
  // Set session config value for ORT_SESSION_OPTIONS_CONFIG_SAVE_MODEL_FORMAT to 'ORT' or 'ONNX' to explicitly
  // specify the format.
  //
  // If session config value is not set, it will be assumed to be ONNX
  // unless the filepath ends in '.ort' (case insensitive).
  std::filesystem::path optimized_model_filepath;

  // enable the memory pattern optimization.
  // The idea is if the input shapes are the same, we could trace the internal memory allocation
  // and generate a memory pattern for future request. So next time we could just do one allocation
  // with a big chunk for all the internal memory allocation.
  // See class 'OrtValuePatternPlanner'.
  bool enable_mem_pattern = true;

  // Enable memory resue in memory planning. Allows to reuse tensor buffer between tensors if they are of
  // the same size. The issue with this is it can lead to memory being held for longer than needed and
  // can impact peak memory consumption.
  bool enable_mem_reuse = true;

  // enable the memory arena on CPU
  // Arena may pre-allocate memory for future usage.
  // set this option to false if you don't want it.
  bool enable_cpu_mem_arena = true;

  // the prefix of the profile file. The current time will be appended to the file name.
  std::basic_string<ORTCHAR_T> profile_file_prefix = ORT_TSTR("onnxruntime_profile_");

  std::string session_logid;  ///< logger id to use for session output

  /// Log severity for the inference session. Applies to session load, initialization, etc.
  /// See https://github.com/microsoft/onnxruntime/blob/main/include/onnxruntime/core/common/logging/severity.h
  /// See https://github.com/microsoft/onnxruntime/blob/main/include/onnxruntime/core/session/onnxruntime_c_api.h#L231 for OrtLoggingLevel mappings
  /// Default = -1 (use default logger severity)
  int session_log_severity_level = -1;
  int session_log_verbosity_level = 0;  ///< VLOG level if debug build and session_log_severity_level is 0 (VERBOSE).

  unsigned max_num_graph_transformation_steps = 10;  // TODO choose a good default here?

  // set graph optimization level
  TransformerLevel graph_optimization_level = TransformerLevel::Level3;

  // controls the size of the thread pool used to parallelize the execution of tasks within individual nodes (ops)
  OrtThreadPoolParams intra_op_param;

  // controls the size of the thread pool used to parallelize the execution of nodes (ops)
  // configuring this makes sense only when you're using parallel executor
  OrtThreadPoolParams inter_op_param;

  // For models with symbolic input dimensions (most commonly batch size), specifies a set of values to override those
  // symbolic dimensions with, keyed by dimension parameters.
  std::vector<FreeDimensionOverride> free_dimension_overrides;

  // By default the session uses its own set of threadpools, unless this is set to false.
  // Use this in conjunction with the CreateEnvWithGlobalThreadPools API.
  bool use_per_session_threads = DEFAULT_USE_PER_SESSION_THREADS;

  bool thread_pool_allow_spinning = true;

  // Deterministic compute is likely not as performant. This option is default to false.
  bool use_deterministic_compute = false;

  // Stores the configurations for this session
  // To add an configuration to this session, call OrtApis::AddSessionConfigEntry
  // The configuration keys and value formats are defined in
  // /include/onnxruntime/core/session/onnxruntime_session_options_config_keys.h
  ConfigOptions config_options;

  std::unordered_map<std::string, const OrtValue*> initializers_to_share_map;

  // See onnxruntime_c_api.h for detailed documentation.
  Status AddInitializer(_In_z_ const char* name, _In_ const OrtValue* val);

#if !defined(ORT_MINIMAL_BUILD) && !defined(DISABLE_EXTERNAL_INITIALIZERS)
  // Customer supplied pre-processed data for external initializers
  InlinedHashMap<std::string, OrtValue> external_initializers;
  Status AddExternalInitializers(gsl::span<const std::string> names, gsl::span<const OrtValue> values);
  InlinedHashMap<PathString, std::pair<char*, size_t>> external_initializer_files_mmap;
  Status AddExternalInitializersFromFilesInMemory(gsl::span<const PathString> file_names,
                                                  gsl::span<std::pair<char*, const size_t>> files_buffers);
#endif

  // custom function callback to create a thread
  OrtCustomCreateThreadFn custom_create_thread_fn = nullptr;

  // custom options to pass to custom_create_thread_fn
  void* custom_thread_creation_options = nullptr;

  // custom function callback to join a thread
  OrtCustomJoinThreadFn custom_join_thread_fn = nullptr;

#if !defined(ORT_MINIMAL_BUILD) || defined(ORT_MINIMAL_BUILD_CUSTOM_OPS)
  // Store handles to custom op libraries so that their lifetimes extend the lifetime of the session options object.
  // Lazily initialized by the first call to SessionOptions::AddCustomOpLibraryHandle().
  std::shared_ptr<LibraryHandles> custom_op_libs;
  void AddCustomOpLibraryHandle(PathString library_name, void* library_handle);
#endif

  // User specified logging func and param
  OrtLoggingFunction user_logging_function = nullptr;
  void* user_logging_param = nullptr;

  void SetLoadCancellationFlag(bool value) noexcept {
    *load_cancellation_flag = value;
  }

  bool IsLoadCancellationFlagSet() const noexcept {
    return *load_cancellation_flag;
  }

  // Load cancellation flag is necessary to be within shared memory as session_options are
  // copied internally and the flag needs to be accessible across all copies.
  std::shared_ptr<std::atomic_bool> load_cancellation_flag = std::make_shared<std::atomic_bool>(false);

  // Policy to guide Execution Provider selection
  EpSelectionPolicy ep_selection_policy = {false,
                                           OrtExecutionProviderDevicePolicy::OrtExecutionProviderDevicePolicy_DEFAULT,
                                           nullptr};

  // Options for generating compile EPContext models were previously stored in session_option.configs as
  // string key/value pairs. To support more advanced options, such as setting input/output buffers, we
  // now have to store EPContext options in a struct of type EpContextModelGenerationOptions.
  // The function GetEpContextGenerationOptions() handles conversion of string key/value pairs to the new
  // struct type.
  bool has_explicit_ep_context_gen_options = false;
  EpContextModelGenerationOptions ep_context_gen_options = {};
  EpContextModelGenerationOptions GetEpContextGenerationOptions() const;
};

inline std::ostream& operator<<(std::ostream& os, const SessionOptions& session_options) {
  os << "Session Options { "
     << " execution_mode:" << session_options.execution_mode
     << " execution_order:" << session_options.execution_order
     << " enable_profiling:" << session_options.enable_profiling
     << " optimized_model_filepath:" << ORT_TSTR_CONVERT_TO_PRINTABLE_STRING(session_options.optimized_model_filepath)
     << " enable_mem_pattern:" << session_options.enable_mem_pattern
     << " enable_mem_reuse:" << session_options.enable_mem_reuse
     << " enable_cpu_mem_arena:" << session_options.enable_cpu_mem_arena
     << " profile_file_prefix:" << ORT_TSTR_CONVERT_TO_PRINTABLE_STRING(session_options.profile_file_prefix)
     << " session_logid:" << session_options.session_logid
     << " session_log_severity_level:" << session_options.session_log_severity_level
     << " session_log_verbosity_level:" << session_options.session_log_verbosity_level
     << " max_num_graph_transformation_steps:" << session_options.max_num_graph_transformation_steps
     << " graph_optimization_level:" << static_cast<int>(session_options.graph_optimization_level)
     << " intra_op_param:" << session_options.intra_op_param
     << " inter_op_param:" << session_options.inter_op_param
     //<< " free_dimension_overrides:"           << session_options.free_dimension_overrides
     << " use_per_session_threads:" << session_options.use_per_session_threads
     << " thread_pool_allow_spinning:" << session_options.thread_pool_allow_spinning
     << " use_deterministic_compute:" << session_options.use_deterministic_compute
     << " ep_selection_policy:" << session_options.ep_selection_policy.policy
     << " config_options: { " << session_options.config_options << " }"
  //<< " initializers_to_share_map:"          << session_options.initializers_to_share_map
#if !defined(ORT_MINIMAL_BUILD) && !defined(DISABLE_EXTERNAL_INITIALIZERS)
  //<< " external_initializers:"             << session_options.external_initializers
  //<< " external_initializer_files:"        << session_options.external_initializer_files
#endif
#if !defined(ORT_MINIMAL_BUILD) || defined(ORT_MINIMAL_BUILD_CUSTOM_OPS)
  //<< " custom_op_libs:" << session_options.custom_op_libs
#endif
     << " }";
  return os;
}

}  // namespace onnxruntime
