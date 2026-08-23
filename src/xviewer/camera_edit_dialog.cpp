#include "camera_edit_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

CameraEditDialog::CameraEditDialog(QWidget* parent, const CameraInfo* existing)
    : QDialog(parent) {
    setWindowTitle(existing ? "编辑摄像头" : "新增摄像头");

    name_edit_ = new QLineEdit(this);
    url_edit_ = new QLineEdit(this);
    url_edit_->setPlaceholderText("主码流地址，比如 rtsp://...");
    sub_url_edit_ = new QLineEdit(this);
    sub_url_edit_->setPlaceholderText("辅码流地址（可选，网格预览优先用这个）");

    auto* form = new QFormLayout();
    form->addRow("名称:", name_edit_);
    form->addRow("地址（主码流）:", url_edit_);
    form->addRow("地址（辅码流）:", sub_url_edit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &CameraEditDialog::OnAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->addLayout(form);
    main_layout->addWidget(buttons);

    if (existing) {
        name_edit_->setText(existing->name);
        url_edit_->setText(existing->url);
        sub_url_edit_->setText(existing->sub_url);
    }

    resize(480, 180);
}

void CameraEditDialog::OnAccept() {
    if (name_edit_->text().trimmed().isEmpty()) {
        QMessageBox::information(this, "提示", "请输入名称");
        return;
    }
    if (url_edit_->text().trimmed().isEmpty()) {
        QMessageBox::information(this, "提示", "请输入主码流地址");
        return;
    }
    accept();
}

CameraInfo CameraEditDialog::Result() const {
    CameraInfo info;
    info.name = name_edit_->text().trimmed();
    info.url = url_edit_->text().trimmed();
    info.sub_url = sub_url_edit_->text().trimmed();
    return info;
}
