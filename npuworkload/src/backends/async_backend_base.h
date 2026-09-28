#pragma once

#include "npu_avs/backend.h"

#include <chrono>
#include <exception>
#include <future>
#include <new>
#include <utility>

namespace npu_avs {

class AsyncBackendBase : public INpuBackend {
protected:
    struct WorkResult {
        BackendStatus status = BackendStatus::Ok;
        InferenceResult inference;
        std::string error;
    };

    void Configure(const WorkloadConfig& cfg) { cfg_ = cfg; }
    virtual WorkResult Execute(const TensorBuffer& input, uint64_t inference_index) = 0;

public:
    bool SetInput(const TensorBuffer& input, std::string& error) override {
        if (pending_.valid() || completed_) {
            error = "cannot set input while an inference is pending or unread";
            return false;
        }
        if (!ValidateTensor(input, error)) return false;
        if (input.data_type != TensorDataType::Int8 ||
            TensorElementCount(input) != cfg_.input_elements) {
            error = "backend expects the configured number of int8 input elements";
            return false;
        }
        input_ = input;
        input_set_ = true;
        return true;
    }

    BackendStatus SubmitInference(uint64_t inference_index, std::string& error) override {
        if (!input_set_) {
            error = "input must be set before inference submission";
            return BackendStatus::Error;
        }
        if (pending_.valid() || completed_) {
            error = "previous inference has not been read";
            return BackendStatus::Error;
        }
        const TensorBuffer input = input_;
        try {
            pending_ = std::async(std::launch::async, [this, input, inference_index]() {
                try {
                    return Execute(input, inference_index);
                } catch (const std::bad_alloc&) {
                    return WorkResult{BackendStatus::AllocationFail, {}, "backend allocation failed"};
                } catch (const std::exception& exception) {
                    return WorkResult{BackendStatus::Error, {}, exception.what()};
                } catch (...) {
                    return WorkResult{BackendStatus::UnknownError, {}, "unknown backend exception"};
                }
            });
        } catch (const std::exception& exception) {
            error = std::string("failed to launch asynchronous inference: ") + exception.what();
            return BackendStatus::Error;
        }
        return BackendStatus::Ok;
    }

    BackendStatus WaitForCompletion(uint32_t timeout_ms, std::string& error) override {
        if (!pending_.valid()) {
            error = "no inference is pending";
            return BackendStatus::Error;
        }
        if (pending_.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
            error = "inference completion timeout";
            return BackendStatus::Timeout;
        }
        WorkResult work = pending_.get();
        if (work.status != BackendStatus::Ok) {
            error = work.error.empty() ? "asynchronous inference failed" : work.error;
            return work.status;
        }
        result_ = std::move(work.inference);
        completed_ = true;
        return BackendStatus::Ok;
    }

    bool ReadOutput(InferenceResult& output, std::string& error) override {
        if (!completed_) {
            error = "inference output is not ready";
            return false;
        }
        output = result_;
        completed_ = false;
        input_set_ = false;
        return true;
    }

    void Destroy() override {
        if (pending_.valid()) {
            try { (void)pending_.get(); } catch (...) {}
        }
        input_ = {};
        result_ = {};
        input_set_ = false;
        completed_ = false;
    }

    virtual ~AsyncBackendBase() { Destroy(); }

    WorkloadConfig cfg_;

private:
    TensorBuffer input_;
    InferenceResult result_;
    std::future<WorkResult> pending_;
    bool input_set_ = false;
    bool completed_ = false;
};

} // namespace npu_avs
