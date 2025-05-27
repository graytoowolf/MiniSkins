#ifndef CFMODLIST_H
#define CFMODLIST_H

#include <QMainWindow>

namespace Ui {
class Cfmodlist;
}

class Cfmodlist : public QMainWindow
{
    Q_OBJECT

public:
    explicit Cfmodlist(QWidget *parent = nullptr);
    ~Cfmodlist();

private:
    Ui::Cfmodlist *ui;
};

#endif // CFMODLIST_H
