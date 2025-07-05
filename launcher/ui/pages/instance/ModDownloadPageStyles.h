#pragma once

#include <QString>

class ModDownloadPageStyles
{
public:
    // 模组项目卡片样式
    static QString getModItemWidgetStyle();
    static QString getIconFrameStyle();
    static QString getTitleLabelStyle();
    static QString getAuthorLabelStyle();
    static QString getDescriptionLabelStyle();
    static QString getStatsLabelStyle();
    static QString getProgressBarStyle();
    static QString getInstallButtonStyle();

    // 加载指示器样式
    static QString getLoadingLabelStyle();

    // 常用尺寸常量
    static constexpr int ICON_SIZE = 68;
    static constexpr int MOD_ITEM_MIN_HEIGHT = 90;
    static constexpr int MOD_ITEM_MAX_HEIGHT = 110;
    static constexpr int BUTTON_WIDTH = 90;
    static constexpr int BUTTON_HEIGHT = 32;
    static constexpr int PROGRESS_BAR_HEIGHT = 16;

    // 间距常量
    static constexpr int LAYOUT_MARGIN = 12;
    static constexpr int LAYOUT_SPACING = 15;
    static constexpr int CONTENT_SPACING = 4;
    static constexpr int STATS_SPACING = 8;
};