// Copyright (c) Microsoft Corporation. All rights reserved.
// Copyright (c) Intel Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/inlined_containers.h"
#include "core/framework/execution_provider.h"
#include "core/framework/model_metadef_id_generator.h"
#include "core/providers/webnn/builders/helper.h"

#include <emscripten.h>
#include <emscripten/val.h>

namespace onnxruntime {
namespace webnn {
class Model;
}

class WebNNExecutionProvider : public IExecutionProvider {
 public:
  explicit WebNNExecutionProvider(const std::string& webnn_device_flags);
  virtual ~WebNNExecutionProvider();

  std::vector<std::unique_ptr<ComputeCapability>>
  GetCapability(const onnxruntime::GraphViewer& graph_viewer,
                const IKernelLookup& /*kernel_registries*/,
                const GraphOptimizerRegistry& /* graph_optimizer_registry */,
                IResourceAccountant* /* resource_accountant */) const override;

  DataLayout GetPreferredLayout() const override { return preferred_layout_; }

  // We implement the Compile that takes FusedNodeAndGraph instances.
  FusionStyle GetFusionStyle() const override { return FusionStyle::FilteredGraphViewer; }

  // WebNN does not support concurrent execution of a kernel.
  bool ConcurrentRunSupported() const override { return false; }

#if !defined(ORT_MINIMAL_BUILD) || defined(ORT_EXTENDED_MINIMAL_BUILD)
  common::Status Compile(const std::vector<FusedNodeAndGraph>& fused_nodes,
                         std::vector<NodeComputeInfo>& node_compute_funcs) override;
#endif

  std::shared_ptr<KernelRegistry> GetKernelRegistry() const override;
  std::unique_ptr<onnxruntime::IDataTransfer> GetDataTransfer() const override;
  std::vector<AllocatorPtr> CreatePreferredAllocators() override;

 private:
  emscripten::val wnn_context_ = emscripten::val::undefined();
  emscripten::val wnn_limits_ = emscripten::val::undefined();

  DataLayout preferred_layout_;
  webnn::WebnnDeviceType wnn_device_type_;
  InlinedHashMap<std::string, std::unique_ptr<onnxruntime::webnn::Model>> models_;
  ModelMetadefIdGenerator metadef_id_generator_;
};
}  // namespace onnxruntime
