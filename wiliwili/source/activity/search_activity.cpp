/**
 * Created by fang on 2022/6/9.
 */

#include "activity/search_activity.hpp"
#include "borealis/platforms/switch/swkbd.hpp"
#include "fragment/search_tab.hpp"

SearchActivity::SearchActivity(const std::string& key) {
    SearchActivity::currentKey = key;
    brls::Logger::debug("SearchActivity: create {}", key);
}

void SearchActivity::onContentAvailable() {
    brls::Logger::debug("SearchActivity: onContentAvailable");
    if (!currentKey.empty()) {
        this->search(currentKey);
    }

    this->registerAction("wiliwili/search/tab"_i18n,
                         brls::ControllerButton::BUTTON_Y,
                         [this](brls::View* view) -> bool {
                             brls::Swkbd::openForText(
                                 [&](std::string text) { this->search(text); },
                                 "wiliwili/home/common/search"_i18n, "", 32,
                                 SearchActivity::currentKey, 0);
                             return true;
                         });

    this->getUpdateSearchEvent()->subscribe(
        [this](const std::string& s) { this->search(s); });
    this->searchTab->passEventToSearchHots(&updateSearchEvent);
}

void SearchActivity::search(const std::string& key) {
    if (key.empty()) return;
    SearchActivity::currentKey = key;
    this->labelSearchKey->setText(key);

    this->searchTab->requestData(key);
}

SearchActivity::~SearchActivity() {
    brls::Logger::debug("SearchActivity: delete");
}

UpdateSearchEvent* SearchActivity::getUpdateSearchEvent() {
    return &this->updateSearchEvent;
}
