#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>

#include <chrono>
#include <future>

namespace MphRead::Qt
{
    // Run a continuation on the UI thread once a future is ready, without
    // blocking it: a 16 ms poll owned by the screen model, gone with it.
    template <typename Result, typename Then>
    void Await(QObject* owner, std::shared_future<Result> task, Then then)
    {
        if (task.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            then(task.get());
            return;
        }
        auto* const timer = new QTimer(owner);
        timer->setInterval(16);
        QObject::connect(timer, &QTimer::timeout, owner, [timer, task, then]() mutable
        {
            if (task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                return;
            }
            timer->stop();
            timer->deleteLater();
            then(task.get());
        });
        timer->start();
    }

    // Post work to the owner's thread from any thread; dropped if the owner is gone.
    template <typename Work>
    void Post(QObject* owner, Work work)
    {
        QMetaObject::invokeMethod(owner, std::move(work), ::Qt::QueuedConnection);
    }
}
