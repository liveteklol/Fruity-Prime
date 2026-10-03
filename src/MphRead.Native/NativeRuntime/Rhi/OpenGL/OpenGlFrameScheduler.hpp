#pragma once

#include "../Submission.hpp"
#include "../BackendError.hpp"
#include "../PresentationScheduler.hpp"
#include <cstdlib>
#include <deque>
#include <exception>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    // Completion of one GL context's command stream, independent of frame
    // slots. Markers also cover commands issued by the native/Skia adapters.
    class OpenGlFrameScheduler final
    {
    public:
        enum class WaitStatus : unsigned
        { AlreadySignaled = 0x911A, Timeout = 0x911B, Satisfied = 0x911C, Failed = 0x911D };
        struct Dispatch final
        {
            void* Context;
            void* (*Fence)(void*);
            WaitStatus (*Wait)(void*, void*, bool, std::uint64_t);
            void (*Delete)(void*, void*);
            void (*Flush)(void*);
            void (*Finish)(void*);
            unsigned (*Error)(void*);
            bool SyncSupported;
        };
        explicit OpenGlFrameScheduler(Dispatch dispatch) : _dispatch(dispatch) {}
        ~OpenGlFrameScheduler() { ReleaseCompleted(true); }
        OpenGlFrameScheduler(const OpenGlFrameScheduler&) = delete;
        OpenGlFrameScheduler& operator=(const OpenGlFrameScheduler&) = delete;

        SubmissionSerial Submit(bool flush = false)
        {
            RequireHealthy();
            // Releases since the last marker were handed this serial; the
            // marker inserted now is the one that covers them.
            _retirementPending = false;
            const auto serial = _progress.Next();
            if (!_dispatch.SyncSupported)
            {
                // GL 2.1 diagnostics without ARB_sync keep their explicit
                // synchronous compatibility path; production desktop uses GLsync.
                Finish();
                _progress.Submitted(serial);
                _progress.Complete(serial);
                return serial;
            }
            // Allocate bookkeeping before asking the driver to accept a marker.
            _fences.push_back({serial, nullptr});
            try
            {
                _fences.back().Sync = _dispatch.Fence(_dispatch.Context);
                if (!_fences.back().Sync) Fail("glFenceSync");
            }
            catch (...) { _fences.pop_back(); throw; }
            _progress.Submitted(serial);
            if (flush) _dispatch.Flush(_dispatch.Context);
            return serial;
        }

        // Resource destructors cannot throw a driver failure. Preserve the
        // first failure for the next explicit operation and retain the object
        // until context teardown: no completion token covers a failed marker.
        // A release does not insert a marker of its own: it takes the serial
        // of the next one, which the next submission, poll, wait or finish
        // inserts. Everything released in between shares that one marker --
        // it follows every use the released objects had, which is all a
        // retirement needs -- instead of one glFenceSync per object.
        SubmissionSerial SubmitForRetirement()
        {
            if (_retirementFailure) return {std::numeric_limits<std::uint64_t>::max()};
            if (!_dispatch.SyncSupported)
            {
                try { return Submit(); }
                catch (const BackendError&)
                {
                    if (!_retirementFailure) _retirementFailure = std::current_exception();
                    return {std::numeric_limits<std::uint64_t>::max()};
                }
            }
            _retirementPending = true;
            ++_aggregatedRetirements;
            return _progress.Next();
        }
        [[nodiscard]] bool RetirementPending() const noexcept { return _retirementPending; }
        [[nodiscard]] std::uint64_t AggregatedRetirements() const noexcept { return _aggregatedRetirements; }

        SubmissionSerial Poll()
        {
            RequireHealthy();
            if (_retirementPending) (void)Submit(true);
            return PollAccepted();
        }

    private:
        SubmissionSerial PollAccepted()
        {
            while (!_fences.empty())
            {
                const auto status = _dispatch.Wait(_dispatch.Context, _fences.front().Sync, false, 0);
                if (status == WaitStatus::Timeout) break;
                RequireCompletion(status);
                _progress.Complete(_fences.front().Serial);
                ReleaseCompleted();
            }
            return Completed();
        }

    public:
        void Wait(SubmissionSerial serial)
        {
            if (_retirementPending && serial == _progress.Next()) (void)Submit(true);
            if (serial > Submitted()) throw std::logic_error("Unsubmitted OpenGL completion token.");
            if (serial <= Poll()) return;
            const auto found = std::find_if(_fences.begin(), _fences.end(),
                [serial](const Entry& entry) { return entry.Serial == serial; });
            if (found == _fences.end()) throw std::logic_error("Missing OpenGL completion fence.");
            ++_hostWaits;
            for (;;)
            {
                const auto status = _dispatch.Wait(_dispatch.Context, found->Sync, true, 1'000'000'000ULL);
                if (status == WaitStatus::Timeout) continue;
                RequireCompletion(status);
                break;
            }
            // Completion of this marker proves completion of all earlier work
            // in the same stream, even when earlier fences were not polled.
            _progress.Complete(serial);
            ReleaseCompleted();
        }

        void Finish()
        {
            RequireHealthy();
            // The pending releases' marker, so that finishing completes them.
            if (_retirementPending) (void)Submit(false);
            ++_hostWaits;
            ++_deviceWideWaits;
            _dispatch.Finish(_dispatch.Context);
            const auto error = _dispatch.Error(_dispatch.Context);
            if (error) Fail("glFinish", error);
            _progress.Complete(Submitted());
            ReleaseCompleted();
        }
        [[nodiscard]] SubmissionSerial Submitted() const noexcept { return _progress.Submitted(); }
        [[nodiscard]] bool WaitForLatest(std::uint64_t timeoutNanoseconds)
        {
            RequireHealthy();
            const auto serial = Submitted();
            if (serial <= PollAccepted()) return true;
            const auto found = std::find_if(_fences.begin(), _fences.end(),
                [serial](const Entry& entry) { return entry.Serial == serial; });
            if (found == _fences.end()) throw std::logic_error("Missing OpenGL presentation frame fence.");
            ++_hostWaits;
            const auto start = _measureWaits ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
            const auto status = _dispatch.Wait(_dispatch.Context, found->Sync, true, timeoutNanoseconds);
            if (_measureWaits)
            { ++_waits.count; _waits.nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count(); }
            if (status == WaitStatus::Timeout) return false;
            RequireCompletion(status);
            _progress.Complete(serial); ReleaseCompleted(); return true;
        }
        [[nodiscard]] SubmissionSerial Completed() const noexcept { return _progress.Completed(); }
        [[nodiscard]] std::uint64_t HostWaits() const noexcept { return _hostWaits; }
        [[nodiscard]] PresentationWaitStatistics PresentationWaits() const noexcept { return _waits; }
        [[nodiscard]] std::uint64_t DeviceWideWaits() const noexcept { return _deviceWideWaits; }
        // Storage/query checks also consume glGetError. Preserve their loss
        // before any later completion check can observe an empty error queue.
        void NoteDeviceLost(std::exception_ptr failure) noexcept
        { if (!_retirementFailure) _retirementFailure = std::move(failure); }

    private:
        struct Entry final { SubmissionSerial Serial; void* Sync; };
        void RequireHealthy() const
        {
            if (_retirementFailure) std::rethrow_exception(_retirementFailure);
        }
        [[noreturn]] void Fail(const char* operation)
        {
            Fail(operation, _dispatch.Error(_dispatch.Context));
        }
        [[noreturn]] void Fail(const char* operation, unsigned error)
        {
            try
            {
                throw BackendError(GraphicsBackend::OpenGl,
                    error == 0x0507 ? BackendErrorKind::DeviceLost
                        : error == 0x0505 ? BackendErrorKind::OutOfMemory : BackendErrorKind::Unknown,
                    error, std::string(operation) + " failed; completion was not established.");
            }
            catch (const BackendError&)
            {
                // glGetError consumes its code. A consumed context-loss error
                // must not make a later glFinish appear successful.
                if (error == 0x0507 && !_retirementFailure) _retirementFailure = std::current_exception();
                throw;
            }
        }
        void RequireCompletion(WaitStatus status)
        {
            if (status != WaitStatus::AlreadySignaled && status != WaitStatus::Satisfied)
                Fail("glClientWaitSync");
        }
        void ReleaseCompleted(bool all = false)
        {
            while (!_fences.empty() && (all || _fences.front().Serial <= Completed()))
            {
                _dispatch.Delete(_dispatch.Context, _fences.front().Sync);
                _fences.pop_front();
            }
        }
        Dispatch _dispatch;
        SubmissionProgress _progress;
        std::deque<Entry> _fences;
        std::exception_ptr _retirementFailure;
        std::uint64_t _hostWaits = 0, _deviceWideWaits = 0;
        bool _retirementPending = false;
        std::uint64_t _aggregatedRetirements = 0;
        bool _measureWaits = std::getenv("FRUITY_RENDER_METRICS") != nullptr;
        PresentationWaitStatistics _waits;
    };
}
