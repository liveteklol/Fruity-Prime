#include "ThumbnailHost.hpp"
#include "MapGen/CustomRooms.hpp"

#include "ThumbnailBatch.hpp"
#include "ThumbnailGenerator.hpp"

#include <chrono>
#include <exception>
#include <utility>

namespace MphRead::Mods
{
    namespace
    {
        [[nodiscard]] std::shared_future<int> CompletedTask(int result)
        {
            std::promise<int> promise;
            promise.set_value(result);
            return promise.get_future().share();
        }

        [[nodiscard]] std::shared_future<int> FaultedTask(std::exception_ptr error)
        {
            std::promise<int> promise;
            promise.set_exception(std::move(error));
            return promise.get_future().share();
        }
    }

    ::MphRead::NativeRuntime::AtomicSharedPtr<IThumbnailHost> ThumbnailHost::_current{};

    std::shared_ptr<IThumbnailHost> ThumbnailHost::Current() noexcept
    {
        return _current.load(std::memory_order_relaxed);
    }

    void ThumbnailHost::Current(std::shared_ptr<IThumbnailHost> value) noexcept
    {
        _current.store(std::move(value), std::memory_order_relaxed);
    }

    bool ThumbnailHost::CanRender()
    {
        return Current() != nullptr || ThumbnailBatch::CanRun();
    }

    std::shared_future<int> ThumbnailHost::RenderMissingAsync(
        const std::function<void(const std::string&)>& report)
    {
        try
        {
            // The views through a Quake 3 level's portals first: the room is
            // built again with them, and its preview is taken after.
            if (ThumbnailBatch::CanRun() && !MapGen::CustomRooms::RoomsWithoutPortalViews().empty())
            {
                return std::async(std::launch::async,
                    [report]
                    {
                        ThumbnailGenerator::EnsurePortalViews(report);
                        return RenderMissingAsync(report).get();
                    }).share();
            }

            std::vector<std::string> missing = ThumbnailGenerator::MissingThumbnails();
            if (missing.empty())
            {
                return CompletedTask(0);
            }

            std::shared_ptr<IThumbnailHost> host = Current();
            if (host != nullptr)
            {
                std::shared_future<int> task = host->RenderAsync(std::move(missing), report);
                if (!task.valid())
                {
                    throw std::future_error(std::future_errc::no_state);
                }
                if (task.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    return task;
                }
                return std::async(std::launch::async,
                    [host = std::move(host), task = std::move(task)]
                    {
                        (void)host;
                        return task.get();
                    }).share();
            }

            if (!ThumbnailBatch::CanRun())
            {
                return CompletedTask(0);
            }

            return std::async(std::launch::async,
                [rooms = std::move(missing), report]
                {
                    return ThumbnailBatch::Run(rooms,
                        ThumbnailBatch::DefaultParallelism(),
                        ThumbnailGenerator::ThumbnailWidth,
                        ThumbnailGenerator::ThumbnailHeight,
                        report);
                }).share();
        }
        catch (...)
        {
            // The C# method is async, so failures before its first await fault
            // the returned task instead of escaping from the call itself.
            return FaultedTask(std::current_exception());
        }
    }
}
