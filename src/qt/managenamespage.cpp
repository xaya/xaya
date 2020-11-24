#include <qt/managenamespage.h>
#include <qt/forms/ui_managenamespage.h>

#include <qt/csvmodelwriter.h>
#include <qt/guiutil.h>
#include <qt/nametablemodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>

#include <QMessageBox>
#include <QMenu>
#include <QSortFilterProxyModel>

ManageNamesPage::ManageNamesPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    platformStyle(platformStyle),
    ui(new Ui::ManageNamesPage),
    model(nullptr),
    walletModel(nullptr),
    proxyModel(nullptr)
{
    ui->setupUi(this);

    // Context menu actions
    QAction *copyNameAction = new QAction(tr("Copy &Name"), this);
    QAction *copyValueAction = new QAction(tr("Copy &Value"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyNameAction);
    contextMenu->addAction(copyValueAction);

    // Connect signals for context menu actions
    connect(copyNameAction, &QAction::triggered, this, &ManageNamesPage::onCopyNameAction);
    connect(copyValueAction, &QAction::triggered, this, &ManageNamesPage::onCopyValueAction);

    connect(ui->tableView, &QTableView::customContextMenuRequested, this, &ManageNamesPage::contextualMenu);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ui->tableView->installEventFilter(this);
}

ManageNamesPage::~ManageNamesPage()
{
    delete ui;
}

void ManageNamesPage::setModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    model = walletModel->getNameTableModel();

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    ui->tableView->horizontalHeader()->setHighlightSections(false);

    // Set column widths
    ui->tableView->horizontalHeader()->resizeSection(
            NameTableModel::Name, 320);
    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(ui->tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ManageNamesPage::selectionChanged);

    selectionChanged();
}

bool ManageNamesPage::eventFilter(QObject *object, QEvent *event)
{
    return QWidget::eventFilter(object, event);
}

void ManageNamesPage::selectionChanged()
{
}

void ManageNamesPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if (index.isValid())
        contextMenu->exec(QCursor::pos());
}

void ManageNamesPage::onCopyNameAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Name);
}

void ManageNamesPage::onCopyValueAction()
{
    GUIUtil::copyEntryData(ui->tableView, NameTableModel::Value);
}

void ManageNamesPage::exportClicked()
{
    // CSV is currently the only supported format
    QString suffixOut = "";
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Registered Names Data"),
            QString(),
            tr("Comma separated file (*.csv)"),
            &suffixOut);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Name", NameTableModel::Name, Qt::EditRole);
    writer.addColumn("Value", NameTableModel::Value, Qt::EditRole);
    writer.addColumn("Name Status", NameTableModel::NameStatus, Qt::EditRole);

    if (!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}
