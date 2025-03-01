//
// Created by fang on 2022/4/23.
//

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "view/video_view.hpp"
#include "view/mpv_core.hpp"
#include "view/video_progress_slider.hpp"
#include "view/svg_image.hpp"
#include "utils/number_helper.hpp"
#include <fmt/core.h>
#include "activity/player_activity.hpp"

using namespace brls;

VideoView::VideoView() {
    mpvCore = &MPVCore::instance();
    this->inflateFromXMLRes("xml/views/video_view.xml");
    this->setHideHighlightBackground(true);
    this->setHideClickAnimation(true);

    input = brls::Application::getPlatform()->getInputManager();

    this->registerBoolXMLAttribute("allowFullscreen", [this](bool value) {
        this->allowFullscreen = value;
        if (!value) {
            this->btnFullscreenIcon->getParent()->setVisibility(
                brls::Visibility::GONE);
            this->registerAction(
                "cancel", brls::ControllerButton::BUTTON_B,
                [this](brls::View* view) -> bool {
                    this->dismiss();
                    return true;
                },
                true);
        }
    });

    this->registerAction(
        "\uE08F", brls::ControllerButton::BUTTON_LB,
        [this](brls::View* view) -> bool {
            mpvCore->command_str("seek -10");
            return true;
        },
        false, true);

    this->registerAction(
        "\uE08E", brls::ControllerButton::BUTTON_RB,
        [this](brls::View* view) -> bool {
            ControllerState state;
            input->updateUnifiedControllerState(&state);
            if (state.buttons[BUTTON_Y]) {
                mpvCore->command_str("seek -10");
            } else {
                mpvCore->command_str("seek +10");
            }
            return true;
        },
        false, true);

    this->registerAction(
        "toggleOSD", brls::ControllerButton::BUTTON_Y,
        [this](brls::View* view) -> bool {
            if (isOSDShown()) {
                this->hideOSD();
            } else {
                this->showOSD(true);
            }
            return true;
        },
        true);

    this->registerAction(
        "toggleDanmaku", brls::ControllerButton::BUTTON_X,
        [this](brls::View* view) -> bool {
            if (mpvCore->showDanmaku) {
                mpvCore->showDanmaku = false;
                this->btnDanmakuIcon->setImageFromSVGRes(
                    "svg/bpx-svg-sprite-danmu-switch-off.svg");
            } else {
                mpvCore->showDanmaku = true;
                this->btnDanmakuIcon->setImageFromSVGRes(
                    "svg/bpx-svg-sprite-danmu-switch-on.svg");
            }
            return true;
        },
        true);

    this->registerMpvEvent();

    osdSlider->getProgressSetEvent()->subscribe([this](float progress) {
        brls::Logger::verbose("Set progress: {}", progress);
        this->showOSD(true);
        mpvCore->command_str(
            fmt::format("seek {} absolute-percent", progress * 100).c_str());
    });

    osdSlider->getProgressEvent()->subscribe([this](float progress) {
        this->showOSD(false);
        leftStatusLabel->setText(
            wiliwili::sec2Time(mpvCore->duration * progress));
    });

    this->addGestureRecognizer(new brls::TapGestureRecognizer(this, [this]() {
        if (isOSDShown()) {
            this->hideOSD();
        } else {
            this->showOSD(true);
        }
    }));

    this->btnToggle->addGestureRecognizer(new brls::TapGestureRecognizer(
        this->btnToggle,
        [this]() {
            if (mpvCore->isPaused()) {
                mpvCore->resume();
            } else {
                mpvCore->pause();
            }
        },
        brls::TapGestureConfig(false, brls::SOUND_NONE, brls::SOUND_NONE,
                               brls::SOUND_NONE)));

    /// 默认允许设置全屏按钮
    this->btnFullscreenIcon->getParent()->addGestureRecognizer(
        new brls::TapGestureRecognizer(
            this->btnFullscreenIcon->getParent(),
            [this]() {
                if (this->isFullscreen()) {
                    this->setFullScreen(false);
                } else {
                    this->setFullScreen(true);
                }
            },
            brls::TapGestureConfig(false, brls::SOUND_NONE, brls::SOUND_NONE,
                                   brls::SOUND_NONE)));

    this->btnDanmakuIcon->getParent()->addGestureRecognizer(
        new brls::TapGestureRecognizer(
            this->btnDanmakuIcon->getParent(),
            [this]() {
                if (mpvCore->showDanmaku) {
                    mpvCore->showDanmaku = false;
                    this->btnDanmakuIcon->setImageFromSVGRes(
                        "svg/bpx-svg-sprite-danmu-switch-off.svg");
                } else {
                    mpvCore->showDanmaku = true;
                    this->btnDanmakuIcon->setImageFromSVGRes(
                        "svg/bpx-svg-sprite-danmu-switch-on.svg");
                }
            },
            brls::TapGestureConfig(false, brls::SOUND_NONE, brls::SOUND_NONE,
                                   brls::SOUND_NONE)));

    this->refreshDanmakuIcon();

    this->registerAction(
        "cancel", brls::ControllerButton::BUTTON_B,
        [this](brls::View* view) -> bool {
            if (this->isFullscreen()) {
                this->setFullScreen(false);
            } else {
                this->dismiss();
            }
            return true;
        },
        true);

    this->registerAction("wiliwili/player/fs"_i18n,
                         brls::ControllerButton::BUTTON_A,
                         [this](brls::View* view) {
                             if (this->isFullscreen()) {
                                 //全屏状态下切换播放状态
                                 this->togglePlay();
                                 this->showOSD(true);
                             } else {
                                 //非全屏状态点击视频组件进入全屏
                                 this->setFullScreen(true);
                             }
                             return true;
                         });

#ifdef __SWITCH__
    // switch平台指明弹幕字体为中文简体
    danmakuFont = brls::Application::getFont(brls::FONT_CHINESE_SIMPLIFIED);
#else
    // 其他平台使用通用字体
    danmakuFont = brls::Application::getFont(brls::FONT_REGULAR);
#endif
}

VideoView::~VideoView() {
    brls::Logger::debug("trying delete VideoView...");
    this->unRegisterMpvEvent();
    brls::Logger::debug("Delete VideoView done");
}

void VideoView::draw(NVGcontext* vg, float x, float y, float width,
                     float height, Style style, FrameContext* ctx) {
    if (!mpvCore->isValid()) return;

    mpvCore->openglDraw(this->getFrame(), this->getAlpha());

    // draw danmaku
    if (mpvCore->danmakuLoaded && mpvCore->showDanmaku) {
        static float SECOND        = 8.0;
        static float CENTER_SECOND = 8.0;
        static float FONT_SIZE     = 30;
        static float LINE_HEIGHT   = FONT_SIZE + 10;
        if (danmakuData.size() == 0) {
            mpvCore->resetDanmakuPosition();
            danmakuData = mpvCore->getDanmakuData();
        }

        // Enable scissoring
        nvgSave(vg);
        nvgIntersectScissor(vg, x, y, width, height);

        nvgFontSize(vg, FONT_SIZE);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFontFaceId(vg, this->danmakuFont);
        nvgTextLineHeight(vg, 1);

        int LINES = height / LINE_HEIGHT;
        if (LINES > 20) LINES = 20;

        //取出需要的弹幕
        int64_t currentTime = getCPUTimeUsec();
        float bounds[4];
        for (size_t j = mpvCore->danmakuIndex; j < this->danmakuData.size();
             j++) {
            auto& i = this->danmakuData[j];
            if (!i.canShow) continue;  // 溢出屏幕外
            if (i.showing) {           // 正在展示中
                if (i.type == 4 || i.type == 5) {
                    //居中弹幕
                    // 根据时间判断是否显示弹幕
                    if (i.time > mpvCore->playback_time ||
                        i.time + CENTER_SECOND < mpvCore->playback_time) {
                        i.canShow = false;
                        continue;
                    }

                    // 画弹幕文字包边
                    nvgFillColor(vg, a(i.borderColor));
                    nvgText(vg, x + width / 2 - i.length / 2 + 1,
                            y + i.line * LINE_HEIGHT + 6, i.msg.c_str(),
                            nullptr);

                    // 画弹幕文字
                    nvgFillColor(vg, a(i.color));
                    nvgText(vg, x + width / 2 - i.length / 2,
                            y + i.line * LINE_HEIGHT + 5, i.msg.c_str(),
                            nullptr);

                    continue;
                }
                //滑动弹幕
                float position = 0;
                if (mpvCore->core_idle) {
                    // 暂停状态弹幕也要暂停
                    position = i.speed * (mpvCore->playback_time - i.time);
                    i.startTime =
                        currentTime - (mpvCore->playback_time - i.time) * 1e6;
                } else {
                    position = i.speed * (currentTime - i.startTime) / 1e6;
                }

                // 根据时间或位置判断是否显示弹幕
                if (position > width + i.length ||
                    i.time + SECOND < mpvCore->playback_time) {
                    i.showing             = false;
                    mpvCore->danmakuIndex = j + 1;
                    continue;
                }

                // 画弹幕文字包边
                nvgFillColor(vg, a(i.borderColor));
                nvgText(vg, x + width - position + 1,
                        y + i.line * LINE_HEIGHT + 6, i.msg.c_str(), nullptr);

                // 画弹幕文字
                nvgFillColor(vg, a(i.color));
                nvgText(vg, x + width - position, y + i.line * LINE_HEIGHT + 5,
                        i.msg.c_str(), nullptr);
                continue;
            }
            if (i.time < mpvCore->playback_time) {
                // 排除已经应该暂停显示的弹幕
                if (i.type == 4 || i.type == 5) {
                    if (i.time + CENTER_SECOND < mpvCore->playback_time) {
                        continue;
                    }
                } else if (i.time + SECOND < mpvCore->playback_time) {
                    mpvCore->danmakuIndex = j + 1;
                    continue;
                }
                i.showing = true;
                nvgTextBounds(vg, 0, 0, i.msg.c_str(), nullptr, bounds);
                i.length  = bounds[2] - bounds[0];
                i.speed   = (width + i.length) / SECOND;
                i.canShow = false;
                for (int k = 0; k < LINES; k++) {
                    if (i.type == 4) {
                        //底部
                        if (i.time < DanmakuItem::centerLines[LINES - k - 1])
                            continue;

                        i.line = LINES - k - 1;
                        DanmakuItem::centerLines[LINES - k - 1] =
                            i.time + CENTER_SECOND;
                        i.canShow = true;
                        break;

                    } else if (i.type == 5) {
                        //顶部
                        if (i.time < DanmakuItem::centerLines[k]) continue;

                        i.line                      = k;
                        DanmakuItem::centerLines[k] = i.time + CENTER_SECOND;
                        i.canShow                   = true;
                        break;
                    } else {
                        //滚动
                        if (i.time < DanmakuItem::lines[k].first ||
                            i.time + width / i.speed <
                                DanmakuItem::lines[k].second)
                            continue;
                        i.line = k;
                        DanmakuItem::lines[k].first =
                            i.time + i.length / i.speed;
                        DanmakuItem::lines[k].second = i.time + SECOND;
                        i.canShow                    = true;
                        i.startTime                  = getCPUTimeUsec();
                        if (mpvCore->playback_time - i.time > 0.2)
                            i.startTime -=
                                (mpvCore->playback_time - i.time) * 1e6;
                        break;
                    }
                }
            } else {
                break;
            }
        }

        nvgRestore(vg);
    }

    // draw osd
    if (wiliwili::unix_time() < this->osdLastShowTime) {
        osdBottomBox->frame(ctx);
        osdTopBox->frame(ctx);
    }

    // hot key
    this->buttonProcessing();

    osdCenterBox->frame(ctx);
}

void VideoView::invalidate() { View::invalidate(); }

void VideoView::onLayout() {
    brls::View::onLayout();

    brls::Rect rect = getFrame();

    if (oldRect.getWidth() == -1) {
        //初始化
        this->oldRect = rect;
    }

    if (!(rect == oldRect)) {
        brls::Logger::debug("Video view size: {} / {} scale: {}",
                            rect.getWidth(), rect.getHeight(),
                            Application::windowScale);
        this->mpvCore->setFrameSize(rect);
    }
    oldRect = rect;
}

void VideoView::setUrl(std::string url, int progress, std::string audio) {
    brls::Logger::debug("set video url: {}", url);

    if (progress < 0) progress = 0;
    std::string extra = "referrer=https://www.bilibili.com";
    if (progress > 0) {
        extra += fmt::format(",start={}", progress);
        brls::Logger::debug("set video progress: {}", progress);
    }
    if (!audio.empty()) {
        extra += fmt::format(",audio-file=\"{}\"", audio);
        brls::Logger::debug("set audio: {}", audio);
    }
    brls::Logger::debug("Extra options: {}", extra);

    const char* cmd[] = {"loadfile", url.c_str(), "replace", extra.c_str(),
                         NULL};
    mpvCore->command_async(cmd);
}

void VideoView::setUrl(const std::vector<EDLUrl>& edl_urls, int progress) {
    std::string url = "edl://";
    std::vector<std::string> urls;
    bool delay_open = true;
    for (auto& i : edl_urls) {
        if (i.length < 0) {
            delay_open = false;
            break;
        }
    }
    for (auto& i : edl_urls) {
        if (!delay_open) {
            urls.emplace_back(fmt::format("%{}%{}", i.url.size(), i.url));
            continue;
        }
        urls.emplace_back(
            "!delay_open,media_type=video;!delay_open,media_type=audio;" +
            fmt::format("%{}%{},length={}", i.url.size(), i.url, i.length));
    }
    url += pystring::join(";", urls);
    this->setUrl(url, progress);
}

void VideoView::resume() { mpvCore->command_str("set pause no"); }

void VideoView::pause() { mpvCore->command_str("set pause yes"); }

void VideoView::stop() {
    const char* cmd[] = {"stop", NULL};
    mpvCore->command_async(cmd);
}

void VideoView::togglePlay() {
    if (this->mpvCore->isPaused()) {
        this->resume();
    } else {
        this->pause();
    }
}

/// OSD
void VideoView::showOSD(bool temp) {
    if (temp)
        this->osdLastShowTime =
            wiliwili::unix_time() + VideoView::OSD_SHOW_TIME;
    else
        this->osdLastShowTime = 0xffffffff;
}

void VideoView::hideOSD() { this->osdLastShowTime = 0; }

bool VideoView::isOSDShown() {
    return wiliwili::unix_time() < this->osdLastShowTime;
}

// Loading
void VideoView::showLoading() {
    centerLabel->setVisibility(brls::Visibility::INVISIBLE);
    osdCenterBox->setVisibility(brls::Visibility::VISIBLE);
}

void VideoView::hideLoading() {
    osdCenterBox->setVisibility(brls::Visibility::GONE);
}

void VideoView::hideDanmakuButton() {
    btnDanmakuIcon->getParent()->setVisibility(brls::Visibility::GONE);
}

void VideoView::setTitle(std::string title) {
    ASYNC_RETAIN
    brls::Threading::sync([ASYNC_TOKEN, title]() {
        ASYNC_RELEASE
        this->videoTitleLabel->setText(title);
    });
}

void VideoView::setOnlineCount(std::string count) {
    ASYNC_RETAIN
    brls::Threading::sync([ASYNC_TOKEN, count]() {
        ASYNC_RELEASE
        this->videoOnlineCountLabel->setText(count);
    });
}

std::string VideoView::getTitle() {
    return this->videoTitleLabel->getFullText();
}

void VideoView::setDuration(std::string value) {
    this->rightStatusLabel->setText(value);
}

void VideoView::setPlaybackTime(std::string value) {
    this->leftStatusLabel->setText(value);
}

void VideoView::setFullscreenIcon(bool fs) {
    if (fs) {
        btnFullscreenIcon->setImageFromSVGRes(
            "svg/bpx-svg-sprite-fullscreen-off.svg");
    } else {
        btnFullscreenIcon->setImageFromSVGRes(
            "svg/bpx-svg-sprite-fullscreen.svg");
    }
}

void VideoView::refreshDanmakuIcon() {
    if (mpvCore->showDanmaku) {
        this->btnDanmakuIcon->setImageFromSVGRes(
            "svg/bpx-svg-sprite-danmu-switch-on.svg");
    } else {
        this->btnDanmakuIcon->setImageFromSVGRes(
            "svg/bpx-svg-sprite-danmu-switch-off.svg");
    }
}

void VideoView::refreshToggleIcon() {
    if (mpvCore->isPaused()) {
        btnToggleIcon->setImageFromSVGRes("svg/bpx-svg-sprite-play.svg");
    } else {
        btnToggleIcon->setImageFromSVGRes("svg/bpx-svg-sprite-pause.svg");
    }
}

void VideoView::setProgress(float value) {
    this->osdSlider->setProgress(value);
}

float VideoView::getProgress() { return this->osdSlider->getProgress(); }

View* VideoView::create() { return new VideoView(); }

bool VideoView::isFullscreen() {
    auto rect = this->getFrame();
    return rect.getHeight() == brls::Application::contentHeight &&
           rect.getWidth() == brls::Application::contentWidth;
}

void VideoView::setFullScreen(bool fs) {
    if (!allowFullscreen) {
        brls::Logger::error("Not being allowed to set fullscreen");
        return;
    }

    if (fs == isFullscreen()) {
        brls::Logger::error("Already set fullscreen state to: {}", fs);
        return;
    }

    brls::Logger::info("VideoView set fullscreen state: {}", fs);
    if (fs) {
        this->unRegisterMpvEvent();
        auto container = new brls::Box();
        auto video     = new VideoView();
        float width    = brls::Application::contentWidth;
        float height   = brls::Application::contentHeight;

        container->setDimensions(width, height);
        video->setDimensions(width, height);
        video->setWidthPercentage(100);
        video->setHeightPercentage(100);

        video->setTitle(this->getTitle());
        video->setDuration(this->rightStatusLabel->getFullText());
        video->setPlaybackTime(this->leftStatusLabel->getFullText());
        video->setProgress(this->getProgress());
        video->showOSD(this->osdLastShowTime != 0xffffffff);
        video->setFullscreenIcon(true);
        video->setHideHighlight(true);
        video->refreshToggleIcon();
        video->setOnlineCount(this->videoOnlineCountLabel->getFullText());
        if (osdCenterBox->getVisibility() == brls::Visibility::GONE) {
            video->hideLoading();
        }
        container->addView(video);
        brls::Application::pushActivity(new brls::Activity(container),
                                        brls::TransitionAnimation::NONE);

        // 将焦点 赋给新的video
        // 已修复：触摸点击全屏，按键返回，会导致焦点丢失
        ASYNC_RETAIN
        brls::sync([ASYNC_TOKEN, video]() {
            ASYNC_RELEASE
            brls::Application::giveFocus(video);
        });
    } else {
        ASYNC_RETAIN
        brls::sync([ASYNC_TOKEN]() {
            ASYNC_RELEASE
            //todo: a better way to get videoView pointer
            PlayerActivity* last = dynamic_cast<PlayerActivity*>(
                Application::getActivitiesStack()
                    [Application::getActivitiesStack().size() - 2]);
            if (last) {
                VideoView* video = dynamic_cast<VideoView*>(
                    last->getView("video/detail/video"));
                if (video) {
                    video->setProgress(this->getProgress());
                    video->showOSD(this->osdLastShowTime != 0xffffffff);
                    video->setDuration(this->rightStatusLabel->getFullText());
                    video->setPlaybackTime(
                        this->leftStatusLabel->getFullText());
                    video->registerMpvEvent();
                    video->refreshToggleIcon();
                    video->refreshDanmakuIcon();
                    video->resetDanmakuPosition();
                    if (osdCenterBox->getVisibility() ==
                        brls::Visibility::GONE) {
                        video->hideLoading();
                    } else {
                        video->showLoading();
                    }
                    // 立刻准确地显示视频尺寸
                    this->mpvCore->setFrameSize(video->getFrame());
                }
            }
            // Pop fullscreen videoView
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
        });
    }
}

void VideoView::setCloseOnEndOfFile(bool value) {
    this->closeOnEndOfFile = value;
}

View* VideoView::getDefaultFocus() { return this; }

View* VideoView::getNextFocus(brls::FocusDirection direction,
                              View* currentView) {
    if (this->isFullscreen()) return this;
    return Box::getNextFocus(direction, currentView);
}

enum ClickState {
    IDLE         = 0,
    PRESS        = 1,
    FAST_RELEASE = 3,
    FAST_PRESS   = 4,
    CLICK_DOUBLE = 5
};

void VideoView::buttonProcessing() {
    ControllerState state;
    input->updateUnifiedControllerState(&state);
    static int64_t rsb_press_time = 0;
    int CHECK_TIME                = 200000;

    // todo: 从框架层面实现点击、双击、长按事件
    static int click_state = ClickState::IDLE;
    switch (click_state) {
        case ClickState::IDLE:
            if (state.buttons[BUTTON_RSB]) {
                mpvCore->command_str("set speed 2.0");
                rsb_press_time = getCPUTimeUsec();
                click_state    = ClickState::PRESS;
            }
            break;
        case ClickState::PRESS:
            if (!state.buttons[BUTTON_RSB]) {
                mpvCore->command_str("set speed 1.0");
                int64_t current_time = getCPUTimeUsec();
                if (current_time - rsb_press_time < CHECK_TIME) {
                    // 点击事件
                    brls::Logger::debug("点击");
                    rsb_press_time = current_time;
                    click_state    = ClickState::FAST_RELEASE;
                } else {
                    click_state = ClickState::IDLE;
                }
            }
            break;
        case ClickState::FAST_RELEASE:
            if (state.buttons[BUTTON_RSB]) {
                mpvCore->command_str("set speed 2.0");
                int64_t current_time = getCPUTimeUsec();
                if (current_time - rsb_press_time < CHECK_TIME) {
                    rsb_press_time = current_time;
                    click_state    = ClickState::FAST_PRESS;
                } else {
                    rsb_press_time = current_time;
                    click_state    = ClickState::PRESS;
                }
            }
            break;
        case ClickState::FAST_PRESS:
            if (!state.buttons[BUTTON_RSB]) {
                int64_t current_time = getCPUTimeUsec();
                if (current_time - rsb_press_time < CHECK_TIME) {
                    rsb_press_time = current_time;
                    // 双击事件
                    mpvCore->command_str("set speed 2.0");
                    click_state = ClickState::CLICK_DOUBLE;
                } else {
                    mpvCore->command_str("set speed 1.0");
                    click_state = ClickState::IDLE;
                }
            }
            break;
        case ClickState::CLICK_DOUBLE:
            brls::Logger::debug("speed lock: 2.0");
            click_state = ClickState::IDLE;
            break;
    }
}

void VideoView::registerMpvEvent() {
    if (registerMPVEvent) {
        brls::Logger::error("VideoView already register MPV Event");
    }
    eventSubscribeID =
        mpvCore->getEvent()->subscribe([this](MpvEventEnum event) {
            // brls::Logger::info("mpv event => : {}", event);
            switch (event) {
                case MpvEventEnum::MPV_RESUME:
                    this->showOSD(true);
                    this->hideLoading();
                    this->btnToggleIcon->setImageFromSVGRes(
                        "svg/bpx-svg-sprite-pause.svg");
                    break;
                case MpvEventEnum::MPV_PAUSE:
                    this->showOSD(false);
                    this->btnToggleIcon->setImageFromSVGRes(
                        "svg/bpx-svg-sprite-play.svg");
                    break;
                case MpvEventEnum::START_FILE:
                    this->showOSD(false);
                    rightStatusLabel->setText("00:00");
                    leftStatusLabel->setText("00:00");
                    osdSlider->setProgress(0);
                    break;
                case MpvEventEnum::LOADING_START:
                    this->showLoading();
                    break;
                case MpvEventEnum::LOADING_END:
                    this->hideLoading();
                    this->resetDanmakuPosition();
                    break;
                case MpvEventEnum::MPV_STOP:
                    // todo: 当前播放结束，尝试播放下一个视频
                    this->hideLoading();
                    this->showOSD(false);
                    break;
                case MpvEventEnum::MPV_LOADED:
                    break;
                case MpvEventEnum::UPDATE_DURATION:
                    rightStatusLabel->setText(
                        wiliwili::sec2Time(mpvCore->duration));
                    break;
                case MpvEventEnum::UPDATE_PROGRESS:
                    leftStatusLabel->setText(
                        wiliwili::sec2Time(mpvCore->video_progress));
                    osdSlider->setProgress(mpvCore->playback_time /
                                           mpvCore->duration);
                    break;
                case MpvEventEnum::DANMAKU_LOADED:
                    mpvCore->danmakuMutex.lock();
                    danmakuData = mpvCore->danmakuData;
                    mpvCore->danmakuMutex.unlock();
                    break;
                case MpvEventEnum::END_OF_FILE:
                    // 播放结束自动取消全屏
                    this->showOSD(false);
                    if (this->closeOnEndOfFile && this->isFullscreen()) {
                        this->setFullScreen(false);
                    }
                    break;
                case MpvEventEnum::CACHE_SPEED_CHANGE:
                    // 仅当加载圈已经开始转起的情况显示缓存
                    if (this->osdCenterBox->getVisibility() !=
                        brls::Visibility::GONE) {
                        if (this->centerLabel->getVisibility() !=
                            brls::Visibility::VISIBLE)
                            this->centerLabel->setVisibility(
                                brls::Visibility::VISIBLE);
                        this->centerLabel->setText(mpvCore->getCacheSpeed());
                    }
                    break;
                default:
                    break;
            }
        });
    registerMPVEvent = true;
}

void VideoView::unRegisterMpvEvent() {
    if (!registerMPVEvent) return;
    mpvCore->getEvent()->unsubscribe(eventSubscribeID);
    registerMPVEvent = false;
}

void VideoView::resetDanmakuPosition() {
    mpvCore->resetDanmakuPosition();
    this->danmakuData = mpvCore->danmakuData;
}