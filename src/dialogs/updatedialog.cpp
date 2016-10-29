#include "updatedialog.h"
#include "ui_updatedialog.h"
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QDebug>
#include <QSettings>
#include <release.h>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMessageBox>
#include <utils/misc.h>
#include <QDir>
#include <QTemporaryFile>

UpdateDialog::UpdateDialog(QWidget *parent, QString changesHtml,
                           QString releaseUrl, QString releaseVersionString,
                           int releaseBuildNumber) :
        MasterDialog(parent),
        ui(new Ui::UpdateDialog) {
    ui->setupUi(this);
    ui->downloadProgressBar->hide();

    ui->changeLogEdit->setHtml(changesHtml);
    ui->versionLabel->setText("Version " + releaseVersionString + " - build " +
                              QString::number(releaseBuildNumber));
    this->releaseVersionString = releaseVersionString;
    this->releaseUrl = releaseUrl;

    QPushButton *button;
    ui->buttonBox->clear();

    _updateButton = new QPushButton(tr("&Update"));
    _updateButton->setProperty("ActionRole", Update);
    _updateButton->setDefault(true);
    // TODO(pbek): use other icon
    _updateButton->setIcon(
            QIcon::fromTheme(
                    "edit-download",
                    QIcon(":/icons/breeze-qownnotes/16x16/edit-download.svg")));
    ui->buttonBox->addButton(_updateButton, QDialogButtonBox::ActionRole);

    button = new QPushButton(tr("&Download latest"));
    button->setProperty("ActionRole", Download);
    button->setDefault(true);
    button->setIcon(
            QIcon::fromTheme(
                    "edit-download",
                    QIcon(":/icons/breeze-qownnotes/16x16/edit-download.svg")));
    ui->buttonBox->addButton(button, QDialogButtonBox::ActionRole);

    button = new QPushButton(tr("&Skip version"));
    button->setProperty("ActionRole", Skip);
    button->setDefault(false);
    button->setIcon(
            QIcon::fromTheme(
                    "media-skip-forward",
                    QIcon(":/icons/breeze-qownnotes/16x16/"
                                  "media-skip-forward.svg")));
    ui->buttonBox->addButton(button, QDialogButtonBox::ActionRole);

    button = new QPushButton(tr("&Cancel"));
    button->setProperty("ActionRole", Cancel);
    button->setIcon(
            QIcon::fromTheme(
                    "dialog-cancel",
                    QIcon(":/icons/breeze-qownnotes/16x16/dialog-cancel.svg")));
    button->setDefault(false);
    ui->buttonBox->addButton(button, QDialogButtonBox::ActionRole);

    connect(this->ui->buttonBox, SIGNAL(clicked(QAbstractButton *)),
            SLOT(dialogButtonClicked(QAbstractButton *)));

    _networkManager = new QNetworkAccessManager();
    QObject::connect(_networkManager, SIGNAL(finished(QNetworkReply *)),
                     this, SLOT(slotReplyFinished(QNetworkReply *)));
}

UpdateDialog::~UpdateDialog() {
    delete ui;
}

int UpdateDialog::exec() {
    setIsUpdateDialogOpen(true);
    int result = MasterDialog::exec();
    setIsUpdateDialogOpen(false);
    return result;
}

void UpdateDialog::closeEvent(QCloseEvent *event) {
    setIsUpdateDialogOpen(false);
}

void UpdateDialog::show() {
    setIsUpdateDialogOpen(true);
    MasterDialog::show();
}

void UpdateDialog::dialogButtonClicked(QAbstractButton *button) {
    int actionRole = button->property("ActionRole").toInt();

    switch (actionRole) {
        case Skip:
        {
            QSettings settings;
            settings.setValue("skipVersion", this->releaseVersionString);
            qDebug() << "skip version";
            break;
        }
        case Download:
        {
            QString release = qApp->property("release").toString();
            // if the release was build by the CI systems download the new
            // release
            if ((release == "Travis CI") || (release == "AppVeyor")) {
                // download the new release
                QDesktopServices::openUrl(QUrl(releaseUrl.toUtf8()));
            } else {
                // open the installation page
                QDesktopServices::openUrl(
                        QUrl("http://www.qownnotes.org/installation"));
            }
            break;
        }
        case Update:
        {
            qDebug() << __func__ << " - 'releaseUrl': " << releaseUrl;
            ui->downloadProgressBar->show();
            _updateButton->setDisabled(true);

            QUrl url(releaseUrl);
            QNetworkRequest networkRequest(url);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
            networkRequest.setAttribute(
                    QNetworkRequest::FollowRedirectsAttribute, true);
#endif

            QNetworkReply *reply = _networkManager->get(networkRequest);

            connect(reply, SIGNAL(downloadProgress(qint64, qint64)),
                    this, SLOT(releaseDownloadProgress(qint64, qint64)));

            break;
        }
        default:
            break;
    }

    if (actionRole != Update) {
        this->close();
    }
}

/**
 * Shows the download progress
 */
void UpdateDialog::releaseDownloadProgress(
        qint64 bytesReceived, qint64 bytesTotal) {
    ui->downloadProgressBar->setMaximum(bytesTotal);
    ui->downloadProgressBar->setValue(bytesReceived);
}

/**
 * Stores the downloaded file
 *
 * @param reply
 */
void UpdateDialog::slotReplyFinished(QNetworkReply *reply) {
    if (reply == NULL) {
        return;
    }

    ui->downloadProgressBar->hide();

    qDebug() << "Reply from " << reply->url().path();
    QByteArray arr = reply->readAll();
    QString data = QString(arr);
    qDebug() << __func__ << " - 'data': " << data;

    if (reply->error() == QNetworkReply::NoError) {
#if defined(Q_OS_MAC)
        QString suffix = "dmg";
#elif defined(Q_OS_WIN)
        QString suffix = "zip";
#elif defined(Q_OS_LINUX)
        QString suffix = "deb";
#else
        QString suffix = "download";
#endif

        QTemporaryFile *file = new QTemporaryFile(
                QDir::tempPath() + QDir::separator() + "QOwnNotes-XXXXXX." +
                suffix);

        // we want to keep the file to be used in the update process
        file->setAutoRemove(false);

        // TODO(pbek): this doesn't show the file path!
        qDebug() << __func__ << " - 'file': " << file->fileName();

        // store file
        if (file->open()) {
            QTextStream out(file);
            out << data;
            file->flush();
            file->close();
        } else {
            QMessageBox::critical(
                    0, tr("Could not store to file"),
                    tr("Could not store to file:\n%1\n\n%2\n\nIs your "
                               "temporary path writable?")
                            .arg(file->fileName(), file->errorString()));
        }
    } else {
        QMessageBox::critical(
                0, tr("Download error"),
                tr("Error while downloading:\n%1").arg(reply->errorString()));

        qWarning() << tr("network error: %1").arg(reply->errorString());
        _updateButton->setDisabled(false);
    }
}

/**
 * Checks if the update dialog is open
 */
bool UpdateDialog::isUpdateDialogOpen() {
    return qApp->property("isUpdateDialogOpen").value<bool>();
}

/**
 * Sets if the update dialog is open
 */
void UpdateDialog::setIsUpdateDialogOpen(bool isOpen) {
    qApp->setProperty("isUpdateDialogOpen", QVariant::fromValue<bool>(isOpen));
}
