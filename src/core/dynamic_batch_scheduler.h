// Copyright (c) 2018-2020, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include "src/core/api.pb.h"
#include "src/core/model_config.h"
#include "src/core/model_config.pb.h"
#include "src/core/scheduler.h"
#include "src/core/status.h"

namespace nvidia { namespace inferenceserver {

// Scheduler that implements dynamic batching.
class DynamicBatchScheduler : public Scheduler {
 public:
  // Create a scheduler to support a given number of runners and a run
  // function to call when a request is scheduled.
  static Status Create(
      const uint32_t runner_id_start, const uint32_t runner_cnt, const int nice,
      const StandardInitFunc& OnInit, const StandardWarmupFunc& OnWarmup,
      const StandardRunFunc& OnSchedule,
      const StandardShapeTensorPeekFunc& OnPeek,
      const bool dynamic_batching_enabled,
      const std::unordered_map<std::string, bool>& enforce_equal_shape_tensors,
      const bool preserve_ordering,
      const std::set<int32_t>& preferred_batch_sizes,
      const uint64_t max_queue_delay_microseconds,
      std::unique_ptr<Scheduler>* scheduler);

  ~DynamicBatchScheduler();

  // \see Scheduler::Enqueue()
  void Enqueue(
      const std::shared_ptr<ModelInferStats>& stats,
      const std::shared_ptr<InferRequestProvider>& request_provider,
      const std::shared_ptr<InferResponseProvider>& response_provider,
      std::function<void(const Status&)> OnComplete) override;

 private:
  DynamicBatchScheduler(
      const uint32_t runner_id_start, const uint32_t runner_cnt,
      const StandardInitFunc& OnInit, const StandardWarmupFunc& OnWarmup,
      const StandardRunFunc& OnSchedule,
      const StandardShapeTensorPeekFunc& OnPeek,
      const bool dynamic_batching_enabled,
      const std::unordered_map<std::string, bool>& enforce_equal_shape_tensors,
      const bool preserve_ordering,
      const std::set<int32_t>& preferred_batch_sizes,
      const uint64_t max_queue_delay_microseconds);
  void SchedulerThread(
      const uint32_t runner_id, const int nice,
      const std::shared_ptr<std::atomic<bool>>& rthread_exit,
      std::promise<bool>* is_initialized);
  Status InitPendingShape(
      const int64_t runner_id, const Scheduler::Payload& request_provider);
  bool CompareWithPendingShape(
      const int64_t runner_id,
      const Scheduler::Payload& request_provider) const;
  uint64_t GetDynamicBatch(const int64_t runner_id);

  // Function the scheduler will call to initialize a runner.
  const StandardInitFunc OnInit_;

  // Function the scheduler will call to warmup a runner.
  const StandardWarmupFunc OnWarmup_;

  // Function the scheduler will call to schedule a payload(s) for
  // execution.
  const StandardRunFunc OnSchedule_;

  // Function the scheduler will call to peek at shape tensors.
  const StandardShapeTensorPeekFunc OnPeek_;

  // True if dynamic batching is enabled.
  const bool dynamic_batching_enabled_;

  // The number of scheduler threads.
  const uint32_t scheduler_thread_cnt_;

  // The number of scheduler threads currently idle.
  uint32_t idle_scheduler_thread_cnt_;

  // Mutex and condvar protecting the scheduling queue.
  std::mutex mu_;
  std::condition_variable cv_;

  // Queue holding inference requests for the model represented by
  // this scheduler.
  std::deque<Scheduler::Payload> queue_;

  std::vector<std::unique_ptr<std::thread>> scheduler_threads_;
  std::vector<std::shared_ptr<std::atomic<bool>>> scheduler_threads_exit_;

  size_t max_preferred_batch_size_;
  std::set<int32_t> preferred_batch_sizes_;
  uint64_t pending_batch_delay_ns_;
  size_t pending_batch_size_;
  size_t pending_batch_queue_cnt_;

  // The input tensors that require shape checking before being
  // allowed in a batch. As a map from the tensor name to a bool. If
  // tensor is in map then its shape must match shape of same tensor
  // in requests already in the batch. If value is "true" then
  // additional tensor is treated as a shape tensor and the values
  // contained in the shape tensor must match same tensor already in
  // the batch.
  const std::unordered_map<std::string, bool> enforce_equal_shape_tensors_;
  std::unordered_map<std::string, std::pair<DimsList, std::vector<int64_t>>>
      pending_batch_shapes_;

  const bool preserve_ordering_;

  // The runner that is currently processing payloads
  int64_t last_processing_runner_id_;

  // Per runner parameters to inform and wait for completion of the
  // particular runner
  std::vector<std::shared_ptr<std::promise<void>>> completion_promises_;
};

}}  // namespace nvidia::inferenceserver
