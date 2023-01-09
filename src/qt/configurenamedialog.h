#ifndef CONFIGURENAMEDIALOG_H
#define CONFIGURENAMEDIALOG_H

#include <qt/platformstyle.h>

#include <QDialog>

#include <optional>

namespace Ui {
    class ConfigureNameDialog;
}

class WalletModel;

/** Dialog for editing an address and associated information.
 */
class ConfigureNameDialog : public QDialog
{
    Q_OBJECT

public:

    explicit ConfigureNameDialog(const PlatformStyle *platformStyle,
                                 const QString &_name, const QString &data,
                                 QWidget *parent = nullptr);
    ~ConfigureNameDialog();

    void setModel(WalletModel *walletModel);
    const QString &getReturnData() const { return returnData; }
    const std::optional<QString> getTransferTo() const
    {
        if (returnTransferTo == "")
        {
            return {};
        }
        return returnTransferTo;
    }

public Q_SLOTS:
    void accept() override;
    void on_addressBookButton_clicked();
    void on_pasteButton_clicked();
    void onDataEdited(const QString &name);

private:
    Ui::ConfigureNameDialog *ui;
    const PlatformStyle *platformStyle;
    QString returnData;
    QString returnTransferTo;
    WalletModel *walletModel;
    const QString name;
};

#endif // CONFIGURENAMEDIALOG_H
