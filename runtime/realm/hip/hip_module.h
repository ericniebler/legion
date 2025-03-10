/* Copyright 2023 Stanford University, NVIDIA Corporation
 *                Los Alamos National Laboratory
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef REALM_HIP_H
#define REALM_HIP_H

#include "realm/realm_config.h"
#include "realm/module.h"
#include "realm/network.h"
#include "realm/atomics.h"

namespace Realm {
  
  namespace NetworkSegmentInfo {
    // HIP device memory - extra is a uintptr_t'd pointer to the GPU
    //  object
    static const MemoryType HipDeviceMem = 3;

    // CUDA managed memory - extra is a uintptr_t'd pointer to _one of_
    //  the GPU objects
    static const MemoryType HipManagedMem = 4;
  };
  
  namespace Hip {

    class GPU;
    class GPUWorker;
    struct GPUInfo;
    class GPUZCMemory;
    class GPUReplHeapListener;

    // our interface to the rest of the runtime
    class HipModule : public Module {
    protected:
      HipModule(RuntimeImpl *_runtime);
      
    public:
      virtual ~HipModule(void);

      static Module *create_module(RuntimeImpl *runtime, std::vector<std::string>& cmdline);

      // do any general initialization - this is called after all configuration is
      //  complete
      virtual void initialize(RuntimeImpl *runtime);

      // create any memories provided by this module (default == do nothing)
      //  (each new MemoryImpl should use a Memory from RuntimeImpl::next_local_memory_id)
      virtual void create_memories(RuntimeImpl *runtime);

      // create any processors provided by the module (default == do nothing)
      //  (each new ProcessorImpl should use a Processor from
      //   RuntimeImpl::next_local_processor_id)
      virtual void create_processors(RuntimeImpl *runtime);

      // create any DMA channels provided by the module (default == do nothing)
      virtual void create_dma_channels(RuntimeImpl *runtime);

      // create any code translators provided by the module (default == do nothing)
      virtual void create_code_translators(RuntimeImpl *runtime);

      // if a module has to do cleanup that involves sending messages to other
      //  nodes, this must be done in the pre-detach cleanup
      virtual void pre_detach_cleanup(void);

      // clean up any common resources created by the module - this will be called
      //  after all memories/processors/etc. have been shut down and destroyed
      virtual void cleanup(void);

    public:
      size_t cfg_zc_mem_size, cfg_zc_ib_size;
      size_t cfg_fb_mem_size, cfg_fb_ib_size;
      bool cfg_use_dynamic_fb;
      size_t cfg_dynfb_max_size;
      unsigned cfg_num_gpus;
      std::string cfg_gpu_idxs;
      unsigned cfg_task_streams, cfg_d2d_streams;
      bool cfg_use_worker_threads, cfg_use_shared_worker, cfg_pin_sysmem;
      bool cfg_fences_use_callbacks;
      bool cfg_suppress_hijack_warning;
      unsigned cfg_skip_gpu_count;
      bool cfg_skip_busy_gpus;
      size_t cfg_min_avail_mem;
      int cfg_task_context_sync; // 0 = no, 1 = yes, -1 = default (based on hijack)
      int cfg_max_ctxsync_threads;
      bool cfg_multithread_dma;
      size_t cfg_hostreg_limit;
      int cfg_d2d_stream_priority;
      bool cfg_use_hip_ipc;

      RuntimeImpl *runtime;

      // "global" variables live here too
      GPUWorker *shared_worker;
      std::map<GPU *, GPUWorker *> dedicated_workers;
      std::vector<GPUInfo *> gpu_info;
      std::vector<GPU *> gpus;
      void *zcmem_cpu_base, *zcib_cpu_base;
      GPUZCMemory *zcmem;
      std::vector<void *> registered_host_ptrs;
      GPUReplHeapListener *rh_listener;

      Mutex hipipc_mutex;
      Mutex::CondVar hipipc_condvar;
      atomic<int> hipipc_responses_needed;
      atomic<int> hipipc_releases_needed;
      atomic<int> hipipc_exports_remaining;
    };

  }; // namespace Hip

}; // namespace Realm 

#endif
