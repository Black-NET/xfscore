#include "xfsnodelist.h"
#include "ui_xfsnodelist.h"

#include "activexfsnode.h"
#include "clientmodel.h"
#include "init.h"
#include "guiutil.h"
#include "xfsnode-sync.h"
#include "xfsnodeconfig.h"
#include "xfsnodeman.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "walletmodel.h"
#include "hybridui/styleSheet.h"
#include <QTimer>
#include <QMessageBox>

int GetOffsetFromUtc()
{
#if QT_VERSION < 0x050200
    const QDateTime dateTime1 = QDateTime::currentDateTime();
    const QDateTime dateTime2 = QDateTime(dateTime1.date(), dateTime1.time(), Qt::UTC);
    return dateTime1.secsTo(dateTime2);
#else
    return QDateTime::currentDateTime().offsetFromUtc();
#endif
}

XFSnodeList::XFSnodeList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::XFSnodeList),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 100;
    int columnStatusWidth = 100;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyXFSnodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyXFSnodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyXFSnodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyXFSnodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyXFSnodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyXFSnodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetXFSnodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetXFSnodes->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetXFSnodes->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetXFSnodes->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetXFSnodes->setColumnWidth(4, columnLastSeenWidth);

    ui->tableWidgetMyXFSnodes->setContextMenuPolicy(Qt::CustomContextMenu);
    SetObjectStyleSheet(ui->tableWidgetMyXFSnodes, StyleSheetNames::TableViewLight);

    QAction *startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyXFSnodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    fFilterUpdated = false;
    nTimeFilterUpdated = GetTime();
    updateNodeList();
}

XFSnodeList::~XFSnodeList()
{
    delete ui;
}

void XFSnodeList::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model) {
        // try to update list when xfsnode count changes
        // connect(clientModel, SIGNAL(strXFSnodesChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void XFSnodeList::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
}

void XFSnodeList::showContextMenu(const QPoint &point)
{
    QTableWidgetItem *item = ui->tableWidgetMyXFSnodes->itemAt(point);
    if(item) contextMenu->exec(QCursor::pos());
}

void XFSnodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH(CXFSnodeConfig::CXFSnodeEntry mne, xfsnodeConfig.getEntries()) {
        if(mne.getAlias() == strAlias) {
            std::string strError;
            CXFSnodeBroadcast mnb;

            bool fSuccess = CXFSnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if(fSuccess) {
                strStatusHtml += "<br>Successfully started xfsnode.";
                mnodeman.UpdateXFSnodeList(mnb);
                mnb.RelayXFSNode();
                mnodeman.NotifyXFSnodeUpdates();
            } else {
                strStatusHtml += "<br>Failed to start xfsnode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void XFSnodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH(CXFSnodeConfig::CXFSnodeEntry mne, xfsnodeConfig.getEntries()) {
        std::string strError;
        CXFSnodeBroadcast mnb;

        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), nOutputIndex);

        if(strCommand == "start-missing" && mnodeman.Has(CTxIn(outpoint))) continue;

        bool fSuccess = CXFSnodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if(fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateXFSnodeList(mnb);
            mnb.RelayXFSNode();
            mnodeman.NotifyXFSnodeUpdates();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d xfsnodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void XFSnodeList::updateMyXFSnodeInfo(QString strAlias, QString strAddr, const COutPoint& outpoint)
{
    bool fOldRowFound = false;
    int nNewRow = 0;

    for(int i = 0; i < ui->tableWidgetMyXFSnodes->rowCount(); i++) {
        if(ui->tableWidgetMyXFSnodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if(nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyXFSnodes->rowCount();
        ui->tableWidgetMyXFSnodes->insertRow(nNewRow);
    }

    xfsnode_info_t infoMn = mnodeman.GetXFSnodeInfo(CTxIn(outpoint));
    bool fFound = infoMn.fInfoValid;

    QTableWidgetItem *aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem *addrItem = new QTableWidgetItem(fFound ? QString::fromStdString(infoMn.addr.ToString()) : strAddr);
    QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(fFound ? infoMn.nProtocolVersion : -1));
    QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(fFound ? CXFSnode::StateToString(infoMn.nActiveState) : "MISSING"));
    QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(fFound ? (infoMn.nTimeLastPing - infoMn.sigTime) : 0)));
    QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M",
                                                                                                   fFound ? infoMn.nTimeLastPing + GetOffsetFromUtc() : 0)));
    QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(fFound ? CBitcoinAddress(infoMn.pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMyXFSnodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyXFSnodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyXFSnodes->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyXFSnodes->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyXFSnodes->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyXFSnodes->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyXFSnodes->setItem(nNewRow, 6, pubkeyItem);
}

void XFSnodeList::updateMyNodeList(bool fForce)
{
    TRY_LOCK(cs_mymnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my xfsnode list only once in MY_MASTERNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_MASTERNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if(nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetXFSnodes->setSortingEnabled(false);
    BOOST_FOREACH(CXFSnodeConfig::CXFSnodeEntry mne, xfsnodeConfig.getEntries()) {
        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        updateMyXFSnodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), COutPoint(uint256S(mne.getTxHash()), nOutputIndex));
    }
    ui->tableWidgetXFSnodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void XFSnodeList::updateNodeList()
{
    TRY_LOCK(cs_mnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in MASTERNODELIST_UPDATE_SECONDS seconds
    // or MASTERNODELIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated
                            ? nTimeFilterUpdated - GetTime() + MASTERNODELIST_FILTER_COOLDOWN_SECONDS
                            : nTimeListUpdated - GetTime() + MASTERNODELIST_UPDATE_SECONDS;

    if(fFilterUpdated) ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if(nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    QString strToFilter;
    ui->countLabel->setText("Updating...");
    ui->tableWidgetXFSnodes->setSortingEnabled(false);
    ui->tableWidgetXFSnodes->clearContents();
    ui->tableWidgetXFSnodes->setRowCount(0);
//    std::map<COutPoint, CXFSnode> mapXFSnodes = mnodeman.GetFullXFSnodeMap();
    std::vector<CXFSnode> vXFSnodes = mnodeman.GetFullXFSnodeVector();
    int offsetFromUtc = GetOffsetFromUtc();

    BOOST_FOREACH(CXFSnode & mn, vXFSnodes)
    {
//        CXFSnode mn = mnpair.second;
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString::fromStdString(mn.addr.ToString()));
        QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(mn.nProtocolVersion));
        QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(mn.GetStatus()));
        QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(mn.lastPing.sigTime - mn.sigTime)));
        QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", mn.lastPing.sigTime + offsetFromUtc)));
        QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString()));

        if (strCurrentFilter != "")
        {
            strToFilter =   addressItem->text() + " " +
                            protocolItem->text() + " " +
                            statusItem->text() + " " +
                            activeSecondsItem->text() + " " +
                            lastSeenItem->text() + " " +
                            pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetXFSnodes->insertRow(0);
        ui->tableWidgetXFSnodes->setItem(0, 0, addressItem);
        ui->tableWidgetXFSnodes->setItem(0, 1, protocolItem);
        ui->tableWidgetXFSnodes->setItem(0, 2, statusItem);
        ui->tableWidgetXFSnodes->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetXFSnodes->setItem(0, 4, lastSeenItem);
        ui->tableWidgetXFSnodes->setItem(0, 5, pubkeyItem);
    }

    ui->countLabel->setText(QString::number(ui->tableWidgetXFSnodes->rowCount()));
    ui->tableWidgetXFSnodes->setSortingEnabled(true);
}

void XFSnodeList::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", MASTERNODELIST_FILTER_COOLDOWN_SECONDS)));
}

void XFSnodeList::on_startButton_clicked()
{
    std::string strAlias;
    {
        LOCK(cs_mymnlist);
        // Find selected node alias
        QItemSelectionModel* selectionModel = ui->tableWidgetMyXFSnodes->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();

        if(selected.count() == 0) return;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strAlias = ui->tableWidgetMyXFSnodes->item(nSelectedRow, 0)->text().toStdString();
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm xfsnode start"),
        tr("Are you sure you want to start xfsnode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void XFSnodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all xfsnodes start"),
        tr("Are you sure you want to start ALL xfsnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void XFSnodeList::on_startMissingButton_clicked()
{

    if(!xfsnodeSync.IsXFSnodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until xfsnode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing xfsnodes start"),
        tr("Are you sure you want to start MISSING xfsnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void XFSnodeList::on_tableWidgetMyXFSnodes_itemSelectionChanged()
{
    if(ui->tableWidgetMyXFSnodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void XFSnodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
