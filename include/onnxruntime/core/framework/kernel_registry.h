// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/framework/op_kernel.h"

namespace onnxruntime {

using KernelCreateMap = std::multimap<std::string, KernelCreateInfo>;
using KernelDefHashes = std::vector<std::pair<std::string, HashValue>>;

using OpIdentifier = std::tuple<std::string, std::string, ONNX_NAMESPACE::OperatorSetVersion>;
using ArgTypeAndIndex = std::pair<ArgType, size_t>;

inline OpIdentifier OpIdFromNode(const Node& node) {
  return OpIdentifier{node.Domain(), node.OpType(), node.SinceVersion()};
}

class KernelTypeStrResolver {
 public:
  gsl::span<const ArgTypeAndIndex> ResolveKernelTypeStr(const OpIdentifier& op_id,
                                                        const std::string& kernel_type_str) const {
    if (auto op_it = op_type_str_map_.find(op_id); op_it != op_type_str_map_.end()) {
      const auto& type_str_map = op_it->second;
      if (const auto type_str_it = type_str_map.find(kernel_type_str); type_str_it != type_str_map.end()) {
        return type_str_it->second;
      }
    }
    ORT_THROW("Failed to resolve type string '", kernel_type_str, "' for op ",
              std::get<1>(op_id), ":", std::get<0>(op_id), "(", std::get<2>(op_id), ")");
  }

  bool Register(const OpIdentifier& op_id, std::string type_str, InlinedVector<ArgTypeAndIndex> args) {
    return op_type_str_map_[op_id].try_emplace(std::move(type_str), std::move(args)).second;
  }

#if !defined(ORT_MINIMAL_BUILD)
  bool RegisterOpSchema(const ONNX_NAMESPACE::OpSchema& op_schema);
#endif  // !defined(ORT_MINIMAL_BUILD)

 private:
  InlinedHashMap<OpIdentifier,
                 InlinedHashMap<std::string,
                                InlinedVector<ArgTypeAndIndex>>>
      op_type_str_map_;
};

/**
 * Each provider has a KernelRegistry. Often, the KernelRegistry only belongs to that specific provider.
 *
 */
class KernelRegistry {
 public:
  KernelRegistry() = default;

  // Register a kernel with kernel definition and function to create the kernel.
  Status Register(KernelDefBuilder& kernel_def_builder, const KernelCreateFn& kernel_creator);

  Status Register(KernelCreateInfo&& create_info);

  Status TryFindKernel(const Node& node, ProviderType exec_provider,
                       const KernelTypeStrResolver& kernel_type_str_resolver,
                       const KernelCreateInfo** out) const;

#if !defined(ORT_MINIMAL_BUILD)

  static bool HasImplementationOf(const KernelRegistry& r, const Node& node,
                                  ProviderType exec_provider) {
    const KernelCreateInfo* info;
    Status st = r.TryFindKernel(node, exec_provider, &info);
    return st.IsOK();
  }

  // factory functions should always return a unique_ptr for maximum flexibility
  // for its clients unless the factory is managing the lifecycle of the pointer
  // itself.
  // TODO(Task:132) Make usage of unique_ptr/shared_ptr as out param consistent
  Status TryCreateKernel(const Node& node, const IExecutionProvider& execution_provider,
                         const std::unordered_map<int, OrtValue>& constant_initialized_tensors,
                         const OrtValueNameIdxMap& mlvalue_name_idx_map, FuncManager& funcs_mgr,
                         const DataTransferManager& data_transfer_mgr,
                         std::unique_ptr<OpKernel>& op_kernel) const;

  // Check if an execution provider can create kernel for a node and return the kernel if so
  Status TryFindKernel(const Node& node, ProviderType exec_provider,
                       const KernelCreateInfo** out) const;

  // Find KernelCreateInfo in instant mode
  Status TryFindKernel(const std::string& op_name, const std::string& domain, const int& version,
                       const std::unordered_map<std::string, MLDataType>& type_constraints,
                       ProviderType exec_provider, const KernelCreateInfo** out) const;

#endif  // !defined(ORT_MINIMAL_BUILD)

  // Try to find the kernel given a kernel def hash.
  bool TryFindKernelByHash(HashValue kernel_def_hash, const KernelCreateInfo** out) const;

  bool IsEmpty() const { return kernel_creator_fn_map_.empty(); }

#ifdef onnxruntime_PYBIND_EXPORT_OPSCHEMA
  // This is used by the opkernel doc generator to enlist all registered operators for a given provider's opkernel
  const KernelCreateMap& GetKernelCreateMap() const {
    return kernel_creator_fn_map_;
  }
#endif

  // Get sorted kernel def key and hash pairs.
  KernelDefHashes ExportKernelDefHashes() const;

 private:
  // Check whether the types of inputs/outputs of the given node match the extra
  // type-constraints of the given kernel. This serves two purposes: first, to
  // select the right kernel implementation based on the types of the arguments
  // when we have multiple kernels, e.g., Clip<float> and Clip<int>; second, to
  // accommodate (and check) mapping of ONNX (specification) type to the onnxruntime
  // implementation type (e.g., if we want to implement ONNX's float16 as a regular
  // float in onnxruntime). (The second, however, requires a globally uniform mapping.)
  //
  // Note that this is not intended for type-checking the node against the ONNX
  // type specification of the corresponding op, which is done before this check.
  //
  // if this function is called before graph partition, then node.provider is not set.
  // In this case, kernel_def.provider must equal to exec_provider
  // otherwise, kernel_def.provider must equal to node.provider. exec_provider is ignored.
  static bool VerifyKernelDef(const Node& node,
                              const KernelDef& kernel_def,
                              const KernelTypeStrResolver& kernel_type_str_resolver,
                              std::string& error_str);

  static std::string GetMapKey(const std::string& op_name, const std::string& domain, const std::string& provider) {
    std::string key(op_name);
    // use the kOnnxDomainAlias of 'ai.onnx' instead of kOnnxDomain's empty string
    key.append(1, ' ').append(domain.empty() ? kOnnxDomainAlias : domain).append(1, ' ').append(provider);
    return key;
  }

  static std::string GetMapKey(const KernelDef& kernel_def) {
    return GetMapKey(kernel_def.OpName(), kernel_def.Domain(), kernel_def.Provider());
  }
  // Kernel create function map from op name to kernel creation info.
  // key is opname+domain_name+provider_name
  KernelCreateMap kernel_creator_fn_map_;

  // map from kernel def hash to entry in kernel_creator_fn_map_
  std::unordered_map<HashValue, KernelCreateMap::iterator> kernel_def_hash_lookup_;
};
}  // namespace onnxruntime
