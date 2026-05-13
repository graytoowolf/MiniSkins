#pragma once

#include "tasks/Task.h"
#include <QFuture>
#include <QFutureWatcher>
#include <atomic>
#include "BaseInstance.h"
#include "SeparatorPrefixTree.h"

class ExportInstanceTask : public Task
{
    Q_OBJECT

public:
    ExportInstanceTask(InstancePtr instance, const QString &output,
                       const QString &prefix,
                       const SeparatorPrefixTree<'/'> &blocked);

protected:
    virtual void executeTask() override;
    virtual bool abort() override;

private:
    void compressFinished();
    bool compressDir();

    InstancePtr m_instance;
    QString m_output;
    QString m_prefix;
    SeparatorPrefixTree<'/'> m_blocked;
    QFuture<bool> m_compressFuture;
    QFutureWatcher<bool> m_compressFutureWatcher;
    std::atomic<bool> m_aborted;
};
