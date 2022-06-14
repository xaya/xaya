#include <qt/buynamespage.h>
#include <qt/forms/ui_buynamespage.h>

#include <interfaces/node.h>
#include <qt/configurenamedialog.h>
#include <qt/guiutil.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>

#include <univalue.h>

#include <QMessageBox>

BuyNamesPage::BuyNamesPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    platformStyle(platformStyle),
    ui(new Ui::BuyNamesPage),
    walletModel(nullptr)
{
    ui->setupUi(this);

    connect(ui->registerNameButton, &QPushButton::clicked, this, &BuyNamesPage::onRegisterNameAction);

    ui->registerName->installEventFilter(this);
}

BuyNamesPage::~BuyNamesPage()
{
    delete ui;
}

void BuyNamesPage::setModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
}

bool BuyNamesPage::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        if (object == ui->registerName)
        {
            ui->registerNameButton->setDefault(true);
        }
    }
    return QWidget::eventFilter(object, event);
}

void BuyNamesPage::onRegisterNameAction()
{
    if (!walletModel)
        return;

    QString name = ui->registerName->text();

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid())
        return;

    ConfigureNameDialog dlg(platformStyle, name, "", this);
    dlg.setModel(walletModel);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString &newValue = dlg.getReturnData();
    const std::optional<QString> transferToAddress = dlg.getTransferTo();

    const QString err_msg = this->firstupdate(name, newValue, transferToAddress);
    if (!err_msg.isEmpty() && err_msg != "ABORTED")
    {
        QMessageBox::critical(this, tr("Name registration error"), err_msg);
        return;
    }

    // reset UI text
    ui->registerName->setText("d/");
    ui->registerNameButton->setDefault(true);
}

QString BuyNamesPage::firstupdate(const QString &name, const std::optional<QString> &value, const std::optional<QString> &transferTo) const
{
    std::string strName = name.toStdString();
    LogPrintf ("wallet attempting name_firstupdate: name=%s\n", strName);

    UniValue params(UniValue::VOBJ);
    params.pushKV ("name", strName);

    if (value)
    {
        params.pushKV ("value", value.value().toStdString());
    }

    if (transferTo)
    {
        UniValue options(UniValue::VOBJ);
        options.pushKV ("destAddress", transferTo.value().toStdString());
        params.pushKV ("options", options);
    }

    std::string walletURI = "/wallet/" + walletModel->getWalletName().toStdString();

    UniValue res;
    try {
       res = walletModel->node().executeRpc("name_firstupdate", params, walletURI);
    }
    catch (const UniValue& e) {
        UniValue message = find_value(e, "message");
        std::string errorStr = message.get_str();
        LogPrintf ("name_firstupdate error: %s\n", errorStr);
        return QString::fromStdString(errorStr);
    }
    return tr ("");
}
