#include "cfmodlist.h"
#include "ui_cfmodlist.h"

Cfmodlist::Cfmodlist(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::Cfmodlist)
{
    ui->setupUi(this);
}

Cfmodlist::~Cfmodlist()
{
    delete ui;
}
