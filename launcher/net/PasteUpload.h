#pragma once
#include "UploadTask.h"

class PasteUpload : public UploadTask
{
    Q_OBJECT
public:
    PasteUpload(QWidget *window, const QString &text, const QString &key = "public");
    virtual ~PasteUpload() = default;

    bool validateText() const override;
    int maxSize() const;

protected:
    void executeTask() override;
    bool parseResult(const QJsonDocument &doc) override;

private:
    QString m_key;
    QString m_pasteID;

public slots:
    void uploadFinished() override;
};