#ifndef XVIEWER_CAMERA_EDIT_DIALOG_H_
#define XVIEWER_CAMERA_EDIT_DIALOG_H_

#include <QDialog>

#include "camera_config.h"

class QLineEdit;

// 新增/编辑摄像头的表单：名称 + 主码流地址 + 辅码流地址。existing 传
// nullptr 表示新增，否则用来回填编辑（不含 id——id 是身份标识，由
// CameraConfig::Set 在写回时从原记录保留，这个表单不需要关心它）。
class CameraEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit CameraEditDialog(QWidget* parent = nullptr, const CameraInfo* existing = nullptr);

    CameraInfo Result() const;

private slots:
    void OnAccept();

private:
    QLineEdit* name_edit_;
    QLineEdit* url_edit_;
    QLineEdit* sub_url_edit_;
};

#endif  // XVIEWER_CAMERA_EDIT_DIALOG_H_
