#include "camera_config.h"

#include <algorithm>

#include <QSettings>

CameraConfig* CameraConfig::Instance() {
    static CameraConfig instance;
    return &instance;
}

CameraConfig::CameraConfig() { Load(); }

void CameraConfig::Add(const CameraInfo& info) {
    CameraInfo added = info;
    added.id = next_id_++;
    cams_.push_back(added);
    Save();
}

CameraInfo CameraConfig::Get(int index) const {
    if (index < 0 || index >= cams_.size()) {
        return CameraInfo();
    }
    return cams_[index];
}

void CameraConfig::Set(int index, const CameraInfo& info) {
    if (index < 0 || index >= cams_.size()) {
        return;
    }
    CameraInfo updated = info;
    updated.id = cams_[index].id;  // id 是身份标识，编辑不应该改变它
    cams_[index] = updated;
    Save();
}

void CameraConfig::Remove(int index) {
    if (index < 0 || index >= cams_.size()) {
        return;
    }
    cams_.removeAt(index);
    Save();
}

int CameraConfig::Count() const { return static_cast<int>(cams_.size()); }

void CameraConfig::Load() {
    QSettings settings;
    int count = settings.beginReadArray("cameras");
    cams_.clear();
    cams_.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        CameraInfo info;
        // 旧版本存的记录没有 id 字段，读到 -1 时下面统一按顺位补发，不会跟
        // 已经存在的 id 冲突（旧记录里也不会有大于 0 的 id）。
        info.id = settings.value("id", -1).toInt();
        info.name = settings.value("name").toString();
        info.url = settings.value("url").toString();
        info.sub_url = settings.value("sub_url").toString();
        cams_.push_back(info);
    }
    settings.endArray();

    next_id_ = 1;
    for (auto& cam : cams_) {
        if (cam.id < 0) {
            cam.id = next_id_;
        }
        next_id_ = std::max(next_id_, cam.id + 1);
    }
}

void CameraConfig::Save() {
    QSettings settings;
    settings.beginWriteArray("cameras");
    for (int i = 0; i < cams_.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("id", cams_[i].id);
        settings.setValue("name", cams_[i].name);
        settings.setValue("url", cams_[i].url);
        settings.setValue("sub_url", cams_[i].sub_url);
    }
    settings.endArray();
}
