#include <qt/configurenamedialog.h>
#include <qt/forms/ui_configurenamedialog.h>

// TODO: How many of these are actually still needed?
#include <names/main.h>
#include <qt/addressbookpage.h>
#include <qt/guiutil.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include <wallet/wallet.h>
#include <names/applications.h>

#include <QMessageBox>
#include <QClipboard>

ConfigureNameDialog::ConfigureNameDialog(const PlatformStyle *platformStyle,
                                         const QString &_name, const QString &data,
                                         QWidget *parent) :
    QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint),
    ui(new Ui::ConfigureNameDialog),
    platformStyle(platformStyle),
    name(_name)
{
    ui->setupUi(this);

    if (platformStyle->getUseExtraSpacing())
        ui->transferToLayout->setSpacing(4);

    GUIUtil::setupAddressWidget(ui->transferTo, this);

    ui->labelName->setText(name);
    ui->dataEdit->setText(data);

    connect(ui->dataEdit, &QLineEdit::textEdited, this, &ConfigureNameDialog::onDataEdited);
    onDataEdited(data);

    returnData = data;

    ui->labelSubmitHint->setText(tr("Name update will take approximately 10 minutes to 2 hours."));
    setWindowTitle(tr("Reconfigure Name"));
}


ConfigureNameDialog::~ConfigureNameDialog()
{
    delete ui;
}

void ConfigureNameDialog::accept()
{
    if (!walletModel)
        return;

    QString addr = ui->transferTo->text();
    std::string data = ui->dataEdit->text().toStdString();

    if (addr != "" && !walletModel->validateAddress(addr))
    {
        ui->transferTo->setValid(false);
        return;
    }

    returnData = ui->dataEdit->text();
    returnTransferTo = ui->transferTo->text();

    if(!IsValidJSONOrEmptyString(data)){
        QMessageBox::StandardButton MessageBoxInvalidJSON = 
        QMessageBox::warning(this, tr("Invalid JSON"),
                tr("Are you sure you want to continue anyway? The inputted JSON data is invalid, and is likely to make the name unresolvable."), 
                QMessageBox::Ok|QMessageBox::Cancel);

        if(MessageBoxInvalidJSON == QMessageBox::Ok){
            QDialog::accept();
        }

    } else if (!IsMinimalJSONOrEmptyString(data)) {
        QMessageBox MessageBoxNonMinimalJSON;
        MessageBoxNonMinimalJSON.setIcon(QMessageBox::Warning);
        MessageBoxNonMinimalJSON.setWindowTitle(tr("Non-minimal JSON"));
        MessageBoxNonMinimalJSON.setText(tr("Are you sure you want to continue anyway? The inputted JSON data is non-minimal, and therefore will waste space as well as incurring added transaction costs when written on the blockchain.")); 
        MessageBoxNonMinimalJSON.addButton(QMessageBox::Ok);
        MessageBoxNonMinimalJSON.addButton(QMessageBox::Cancel);
        MessageBoxNonMinimalJSON.addButton(tr("Minimalise JSON"), QMessageBox::ActionRole);
        
        MessageBoxNonMinimalJSON.exec();

        QMessageBox::ButtonRole reply = MessageBoxNonMinimalJSON.buttonRole(MessageBoxNonMinimalJSON.clickedButton());

        if(reply == QMessageBox::AcceptRole){
            QDialog::accept();
        } else if(reply == QMessageBox::ActionRole){
            
            std::string minimalJSONData = GetMinimalJSON(data);
            ui->dataEdit->setText(QString::fromStdString(minimalJSONData));

            returnData = QString::fromStdString(minimalJSONData);
            data = minimalJSONData;

            QDialog::accept();
        }

    } else {
        QDialog::accept();
    }

}

void ConfigureNameDialog::setModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
}

void ConfigureNameDialog::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->transferTo->setText(QApplication::clipboard()->text());
}

void ConfigureNameDialog::on_addressBookButton_clicked()
{
    if (!walletModel)
        return;

    AddressBookPage dlg(
        // platformStyle
        platformStyle,
        // mode
        AddressBookPage::ForSelection,
        // tab
        AddressBookPage::SendingTab,
        // *parent
        this);
    dlg.setModel(walletModel->getAddressTableModel());
    if (dlg.exec())
        ui->transferTo->setText(dlg.getReturnValue());
}

void ConfigureNameDialog::onDataEdited(const QString &name)
{
    ui->dataSize->setText(tr("%1 / %2").arg(name.size()).arg(MAX_VALUE_LENGTH_UI));
    ui->dataSize->resize(ui->dataSize->fontMetrics().horizontalAdvance(ui->dataSize->text()), ui->dataSize->height());

    std::string data = ui->dataEdit->text().toStdString();
    
    if(IsMinimalJSONOrEmptyString(data)){ 
        ui->labelValidJSON->setText(tr("Valid and minimal JSON data."));
    } else if(IsValidJSONOrEmptyString(data)){
        ui->labelValidJSON->setText(tr("JSON data is not minimal."));
    } else {
        ui->labelValidJSON->setText(tr("JSON data inputted is invalid."));
    }
}

