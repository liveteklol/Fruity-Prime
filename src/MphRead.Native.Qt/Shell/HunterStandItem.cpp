#include "HunterStandItem.hpp"

#include "../../MphRead.Native/Mods/Launcher/Portable/LaunchPlan.hpp"
#include "../../MphRead.Native/Mods/Render/LauncherHunter.hpp"

#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <vector>

namespace MphRead::Qt
{
    namespace
    {
        std::vector<HunterStandItem*>& Live()
        {
            static std::vector<HunterStandItem*> items;
            return items;
        }

        [[nodiscard]] bool Showing(const QQuickItem* item)
        {
            for (const QQuickItem* at = item; at != nullptr; at = at->parentItem())
            {
                if (!at->isVisible() || at->opacity() <= 0.0)
                {
                    return false;
                }
            }
            return item->window() != nullptr;
        }
    }

    HunterStandItem::HunterStandItem(QQuickItem* parent) : QQuickItem(parent)
    {
        Live().push_back(this);
    }

    HunterStandItem::~HunterStandItem()
    {
        auto& live = Live();
        live.erase(std::remove(live.begin(), live.end(), this), live.end());
        if (live.empty())
        {
            ::MphRead::Mods::Render::LauncherHunter::Wanted(false);
        }
    }

    void HunterStandItem::SetHunter(int value)
    {
        if (value != _hunter)
        {
            _hunter = value;
            emit changed();
        }
    }

    void HunterStandItem::SetSuit(int value)
    {
        if (value != _suit)
        {
            _suit = value;
            emit changed();
        }
    }

    void HunterStandItem::Publish(double windowWidth, double windowHeight)
    {
        using ::MphRead::Mods::Render::LauncherHunter;
        for (HunterStandItem* const item : Live())
        {
            if (!Showing(item) || item->width() <= 1 || item->height() <= 1
                || windowWidth <= 0 || windowHeight <= 0)
            {
                continue;
            }
            const QRectF box = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            // Random shows one of the seven, as the launcher rerolls it.
            const ::MphRead::Hunter hunter = item->_hunter >= 7
                ? ::MphRead::Mods::Launcher::Hunters::Resolve(::MphRead::Hunter::Random)
                : static_cast<::MphRead::Hunter>(item->_hunter);
            LauncherHunter::Wanted(true);
            LauncherHunter::Hunter(hunter);
            LauncherHunter::Suit(std::clamp(item->_suit, 0, 3));
            LauncherHunter::Left(static_cast<float>(box.left() / windowWidth));
            LauncherHunter::Top(static_cast<float>(box.top() / windowHeight));
            LauncherHunter::Right(static_cast<float>(box.right() / windowWidth));
            LauncherHunter::Bottom(static_cast<float>(box.bottom() / windowHeight));
            return;
        }
        LauncherHunter::Wanted(false);
    }
}
