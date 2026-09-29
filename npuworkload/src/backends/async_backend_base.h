#pragma once

#include "npu_avs/backend.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace npu_avs {

class AsyncBackendBase : public INpuBackend {
protected:
    struct WorkResult {
        BackendStatus status = BackendStatus::Ok;
        InferenceResult inference;
        std::string error;
    };

    using WorkTask = std::function<WorkResult(const std::atomic<bool>& cancel_requested)>;

    void Configure(const WorkloadConfig& cfg, const ProfileSpec& profile) {
        cfg_ = cfg;
        profile_ = profile;
    }

    virtual WorkTask MakeTask(const TensorSet& inputs, uint64_t inference_index) const = 0;

public:
    BackendStatus SetInputs(const TensorSet& inputs, std::string& error) override {
        if (task_state_ || completed_) {
            error = "cannot set inputs while an inference is pending or unread";
            return BackendStatus::Error;
        }
        if (!ValidateTensorSet(inputs, profile_.inputs, error)) return BackendStatus::Error;
        inputs_ = inputs;
        inputs_set_ = true;
        return BackendStatus::Ok;
    }

    BackendStatus SubmitInference(uint64_t inference_index, std::string& error) override {
        if (!inputs_set_) {
            error = "inputs must be set before inference submission";
            return BackendStatus::Error;
        }
        if (task_state_ || worker_.joinable() || completed_) {
            error = "previous inference has not been read or destroyed";
            return BackendStatus::Error;
        }

        WorkTask task;
        try {
            task = MakeTask(inputs_, inference_index);
        } catch (const std::exception& exception) {
            error = std::string("failed to prepare asynchronous inference: ") + exception.what();
            return BackendStatus::Error;
        }
        if (!task) {
            error = "backend did not provide an inference task";
            return BackendStatus::Error;
        }

        task_state_ = std::make_shared<TaskState>();
        const std::shared_ptr<TaskState> state = task_state_;
        try {
            worker_ = std::thread([state, task = std::move(task)]() mutable {
                WorkResult work;
                try {
                    work = task(state->cancel_requested);
                } catch (const std::bad_alloc&) {
                    work = {BackendStatus::AllocationFail, {}, "backend allocation failed"};
                } catch (const std::exception& exception) {
                    work = {BackendStatus::Error, {}, exception.what()};
                } catch (...) {
                    work = {BackendStatus::UnknownError, {}, "unknown backend exception"};
                }
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->work = std::move(work);
                    state->done = true;
                }
                state->condition.notify_all();
            });
        } catch (const std::exception& exception) {
            task_state_.reset();
            error = std::string("failed to launch asynchronous inference: ") + exception.what();
            return BackendStatus::Error;
        }
        return BackendStatus::Ok;
    }

    BackendStatus WaitForCompletion(uint32_t timeout_ms, std::string& error) override {
        if (!task_state_) {
            error = "no inference is pending";
            return BackendStatus::Error;
        }
        WorkResult work;
        {
            std::unique_lock<std::mutex> lock(task_state_->mutex);
            if (!task_state_->condition.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                    [this]() { return task_state_->done; })) {
                task_state_->cancel_requested.store(true, std::memory_order_relaxed);
                error = "inference completion timeout";
                return BackendStatus::Timeout;
            }
            work = std::move(task_state_->work);
        }
        if (worker_.joinable()) worker_.join();
        task_state_.reset();
        inputs_set_ = false;
        if (work.status != BackendStatus::Ok) {
            error = work.error.empty() ? "asynchronous inference failed" : work.error;
            return work.status;
        }
        if (!ValidateTensorSet(work.inference.outputs, profile_.outputs, error)) {
            return BackendStatus::Error;
        }
        result_ = std::move(work.inference);
        completed_ = true;
        return BackendStatus::Ok;
    }

    BackendStatus ReadOutputs(InferenceResult& output, std::string& error) override {
        if (!completed_) {
            error = "inference outputs are not ready";
            return BackendStatus::Error;
        }
        output = std::move(result_);
        result_ = {};
        completed_ = false;
        return BackendStatus::Ok;
    }

    BackendStatus Destroy(uint32_t timeout_ms, std::string& error) override {
        BackendStatus status = BackendStatus::Ok;
        if (task_state_) {
            task_state_->cancel_requested.store(true, std::memory_order_relaxed);
            bool done = false;
            {
                std::unique_lock<std::mutex> lock(task_state_->mutex);
                done = task_state_->condition.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                    [this]() { return task_state_->done; });
            }
            if (worker_.joinable()) {
                if (done) worker_.join();
                else worker_.detach();
            }
            if (!done) {
                error = "backend teardown exceeded its cancellation grace period";
                status = BackendStatus::Timeout;
            }
        } else if (worker_.joinable()) {
            worker_.join();
        }
        task_state_.reset();
        inputs_.clear();
        result_ = {};
        inputs_set_ = false;
        completed_ = false;
        return status;
    }

    ~AsyncBackendBase() override {
        std::string ignored;
        (void)Destroy(50U, ignored);
    }

protected:
    WorkloadConfig cfg_;
    ProfileSpec profile_;

private:
    struct TaskState {
        std::mutex mutex;
        std::condition_variable condition;
        std::atomic<bool> cancel_requested{false};
        bool done = false;
        WorkResult work;
    };

    TensorSet inputs_;
    InferenceResult result_;
    std::shared_ptr<TaskState> task_state_;
    std::thread worker_;
    bool inputs_set_ = false;
    bool completed_ = false;
};

} // namespace npu_avs
