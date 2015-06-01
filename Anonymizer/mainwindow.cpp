#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QInputDialog>

#include <filehandler.h>

#include <string>

#include <iostream>

#include <QDir>
#include <QDirIterator>

#include <QMessageBox>

#include <DirMaker.h>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

#ifdef __APPLE__
#include <qtconcurrentrun.h>
#else
#include <QtConcurrent/QtConcurrent> /*Linux-Windows*/
#endif
#include <QThread>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("MUSIC Dicom Anonymisation Tool");
    this->firstInput = true;
    this->isAnon = false;
    this->ui->radioZip->setChecked(true);
    this->dateString = "";
    this->nameString = "";
    this->nameLength = 0;
    this->dateLength = 0;
    this->doNotClose = false;
    this->ui->progressBar->setVisible(false);
    this->ui->SaveOpenText->setVisible(false);
    this->fileSizeVector = 0;
    this->fileHandler = 0;

    // old stuff - keeping just in case we want to revert
    this->ui->savePushButton->setVisible(false);
    this->ui->setStringsButton->setVisible(false);

    // normal stuff

    this->ui->radioZip->setVisible(false);
    this->ui->radioFolder->setVisible(false);
    this->ui->anonPushButton->setVisible(false);

    this->ui->newDobBox->setVisible(false);
    this->ui->newNameBox->setVisible(false);
    this->ui->NewDobLabel->setVisible(false);
    this->ui->newNameLabel->setVisible(false);
    this->ui->folderNameLabel->setAlignment(Qt::AlignRight);
    this->ui->folderNameLabel->setText("");
    this->ui->folderNameLabel->setVisible(false);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_actionOpen_Folder_triggered()
{
    // Get all files and read them into memory
    // prompt to close if we already have a file
    if (this->doNotClose)
    {
        QMessageBox aMessageBox;
        aMessageBox.setText("Please wait until process has finished!");
        aMessageBox.exec();
        return;
    }

    if (fileSizeVector && !fileSizeVector->empty())
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "File already open!", "Close current file?",
                                      QMessageBox::Yes|QMessageBox::No);

        if (reply == QMessageBox::Yes)
        {
            if (!closeFiles())
                return;
        }
        else
            return;
    }

    QString folderName = QFileDialog::getExistingDirectory(this, "Open Directory",
                                                           "",
                                                           QFileDialog::ShowDirsOnly
                                                           | QFileDialog::DontResolveSymlinks | QFileDialog::DontUseNativeDialog);

    if (!folderName.size())
    {
        return;
    }

    this->doNotClose = true;

    this->ui->folderNameLabel->setText(folderName);
    this->ui->folderNameLabel->setVisible(true);
    this->ui->SaveOpenText->setText("Opening...");
    this->ui->SaveOpenText->setVisible(true);
    this->ui->progressBar->setVisible(true);


    if (!fileHandler)
        fileHandler = new FileHandler;
    connect(fileHandler, SIGNAL(percentageProcessed(double)), this, SLOT(updateProgress(double)), Qt::QueuedConnection);

    connect(&this->watcher,SIGNAL(finished()),this, SLOT(dataLoaded()));

    this->watcher.setFuture(QtConcurrent::run(fileHandler, &FileHandler::getFileSizeVector, this, folderName, &listOfDirs));
}

void MainWindow::on_actionClose_triggered()
{
    exit(0);
}

void MainWindow::on_newPushButton_clicked()
{
    // if we have already opened at least one file this session,
    // we need to prompt

    if (!closeFiles())
        return;

    if (this->doNotClose)
        return;

    this->ui->newDobBox->setVisible(false);
    this->ui->newNameBox->setVisible(false);
    this->ui->NewDobLabel->setVisible(false);
    this->ui->newNameLabel->setVisible(false);

    this->dateString = this->ui->newDobBox->text().toStdString();
    this->nameString = this->ui->newNameBox->text().toStdString();

    if (!firstInput && (this->nameString != "" || this->dateString != ""))
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "New Session?", "Remove anonymisation names?",
                                      QMessageBox::Yes|QMessageBox::No);

        if (reply == QMessageBox::No)
        {
            this->ui->newDobBox->setVisible(true);
            this->ui->newNameBox->setVisible(true);
            this->ui->NewDobLabel->setVisible(true);
            this->ui->newNameLabel->setVisible(true);
        }
        else
        {
            this->nameString = "";
            this->dateString = "";
            this->ui->newDobBox->clear();
            this->ui->newNameBox->clear();


        }
    }

    this->isAnon = false;
}

void MainWindow::on_setStringsButton_clicked()
{
    QString nameResult = QInputDialog::getText(0, "Enter string", "Name:");

    std::string nameResultStd = nameResult.toStdString();

    this->nameString = nameResultStd;

    QString dateResult = QInputDialog::getText(0, "Enter string", "Birthdate:");

    std::string dateResultStd = dateResult.toStdString();

    this->dateString = dateResultStd;
}

void MainWindow::on_anonPushButton_clicked()
{
    if (this->doNotClose)
    {
        QMessageBox aMessageBox;
        aMessageBox.setText("Please wait until process has finished!");
        aMessageBox.exec();
        return;
    }

    if (!fileHandler)
        fileHandler = new FileHandler;


    this->dateString = this->ui->newDobBox->text().toStdString();
    this->nameString = this->ui->newNameBox->text().toStdString();

    if (this->dateString != "" && this->nameString != "")
    {
        if (!fileSizeVector || fileSizeVector->empty())
        {
            QMessageBox aMessageBox;
            aMessageBox.setText("No files opened!");
            aMessageBox.exec();
        }
        if (this->dateString.length() > this->dateLength)
        {
            QMessageBox aMessageBox;
            aMessageBox.setText("New DoB is too long! Aborting!");
            aMessageBox.exec();
            return;
        }

        if (this->nameString.length() > this->nameLength)
        {
            QMessageBox aMessageBox;
            aMessageBox.setText("New name is too long! Aborting!");
            aMessageBox.exec();
            return;
        }

        for (unsigned int i = 0; i < fileSizeVector->size(); i++)
        {
            char* filePointer = (*fileSizeVector)[i].filePointer;
            size_t size = (*fileSizeVector)[i].size;
            size_t dataLength = 0;
            char* name = fileHandler->SeekDicomTag(filePointer, 0x00100010, size, dataLength);
            char* date = fileHandler->SeekDicomTag(filePointer, 0x00100030, size, dataLength);

            for (unsigned int i = 0; i < nameLength; i++)
            {
                // zero the content
                name[i] = 0;
            }

            for (unsigned int i = 0; i < dateLength; i++)
            {
                date[i] = 0;
            }

            for (unsigned int i = 0; i < nameString.length(); i++)
            {
                name[i] = nameString[i];
            }

            for (unsigned int i = 0; i < dateString.length(); i++)
            {
                date[i] = dateString[i];
            }
        }

        this->isAnon = true;

        if (this->ui->radioFolder->isChecked())
        {
            saveFolder();
        }
        else if (this->ui->radioZip->isChecked())
        {
            saveZip();
        }

    }
    else
    {
        QMessageBox aMessageBox;
        aMessageBox.setText("No anonymisation strings set!");
        aMessageBox.exec();
    }

    this->firstInput = false;
}

void MainWindow::on_savePushButton_clicked()
{

    if (!fileSizeVector || fileSizeVector->empty())
    {
        QMessageBox aMessageBox;
        aMessageBox.setText("No data loaded!");
        aMessageBox.exec();
        return;
    }
    if (!this->isAnon)
    {
        // throw dialogue
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "", "Data not anonymised! Continue anyway?",
                                      QMessageBox::Yes|QMessageBox::No);

        if (reply == QMessageBox::No)
            return;
    }
}

void MainWindow::saveZip()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                               "",
                               tr("Zip (*.zip)"), 0, QFileDialog::DontUseNativeDialog);

    if (fileName == "")
        return;

    this->ui->SaveOpenText->setText("Saving...");
    this->ui->SaveOpenText->setVisible(true);
    this->ui->progressBar->setVisible(true);

    QFileInfo aFileInfo(fileName);

    QString fullFileName = fileName;

    fileName.remove(aFileInfo.absoluteDir().absolutePath() + "/");

    if (!fileHandler)
        fileHandler = new FileHandler;

    connect(fileHandler, SIGNAL(percentageProcessed(double)), this, SLOT(updateProgress(double)), Qt::QueuedConnection);
    connect(fileHandler, SIGNAL(finished(bool)), this, SLOT(onComplete(bool)), Qt::QueuedConnection);

    this->doNotClose = true;
    QtConcurrent::run(fileHandler, &FileHandler::createZip, fullFileName, fileSizeVector);
}

void MainWindow::saveFolder()
{
    QString folderName = QFileDialog::getExistingDirectory(this, "Save Directory",
                                                       "",
                                                       QFileDialog::ShowDirsOnly
                                                       | QFileDialog::DontResolveSymlinks | QFileDialog::DontUseNativeDialog);

    if (folderName == "")
        return;

    this->ui->SaveOpenText->setText("Saving...");
    this->ui->SaveOpenText->setVisible(true);
    this->ui->progressBar->setVisible(true);

    for (int p = 0; p < listOfDirs.size(); p++)
    {
        std::string newSubDir = folderName.toStdString() + this->listOfDirs[p].toStdString();

        if (!isDirExist(newSubDir))
        {
            makePath(newSubDir);
        }
    }

    if (!fileHandler)
        fileHandler = new FileHandler;

    connect(fileHandler, SIGNAL(percentageProcessed(double)), this, SLOT(updateProgress(double)), Qt::QueuedConnection);
    connect(fileHandler, SIGNAL(finished(bool)), this, SLOT(onComplete(bool)), Qt::QueuedConnection);

    this->doNotClose = true;
    QtConcurrent::run(fileHandler, &FileHandler::createFolder, folderName, fileSizeVector);

}

void MainWindow::on_openFolderPushButton_clicked()
{
    on_actionOpen_Folder_triggered();
}

bool MainWindow::closeFiles()
{
    if (this->doNotClose)
    {
        QMessageBox aMessageBox;
        aMessageBox.setText("Please wait until process has finished!");
        aMessageBox.exec();
        return false;
    }

    if (fileSizeVector)
    {
        for (unsigned int i = 0; i < fileSizeVector->size(); i++)
            delete [] (*fileSizeVector)[i].filePointer;
        fileSizeVector->clear();

        delete fileSizeVector;
        fileSizeVector = 0;
    }

    if (fileHandler)
    {
        delete fileHandler;
        fileHandler = 0;
    }

    if (this->ui->newDobBox->text() == "" && this->ui->newNameBox->text() == "")
    {
        this->ui->newDobBox->setVisible(false);
        this->ui->newNameBox->setVisible(false);
        this->ui->NewDobLabel->setVisible(false);
        this->ui->newNameLabel->setVisible(false);
    }

    this->ui->folderNameLabel->setText("");
    this->ui->folderNameLabel->setVisible(false);

    this->ui->radioZip->setVisible(false);
    this->ui->radioFolder->setVisible(false);
    this->ui->anonPushButton->setVisible(false);

    return true;
}

void MainWindow::on_closeFilePushbutton_clicked()
{
    closeFiles();
}

void MainWindow::updateProgress(double tick)
{
    this->ui->progressBar->setValue(tick);
}

void MainWindow::onComplete(bool success)
{
    if (!success)
    {
        QMessageBox aMessageBox;
        aMessageBox.setText("A problem occured. Please retry.");
        aMessageBox.exec();
    }
    this->doNotClose = false;
    this->ui->SaveOpenText->setVisible(false);
    this->ui->progressBar->setVisible(false);
}

void MainWindow::dataLoaded()
{
    fileSizeVector = this->watcher.result();
    if (!fileSizeVector || fileSizeVector->empty())
    {
        QMessageBox aMessageBox;
        aMessageBox.setText("Load failed!");
        aMessageBox.exec();

        this->ui->SaveOpenText->setVisible(false);
        this->ui->progressBar->setVisible(false);
        this->ui->folderNameLabel->setText("");
        this->ui->folderNameLabel->setVisible(false);
        return;
    }

    this->firstInput = false;

    // read first file and check max lengths of key tags (name and date of birth)
    size_t dataLength = 0;
    char* filePointer = (*fileSizeVector)[0].filePointer;
    size_t size = (*fileSizeVector)[0].size;

    if (!fileHandler)
        fileHandler = new FileHandler;

    fileHandler->SeekDicomTag(filePointer, 0x00100010, size, dataLength);
    this->nameLength = dataLength;

    fileHandler->SeekDicomTag(filePointer, 0x00100030, size, dataLength);
    this->dateLength = dataLength;


    this->ui->SaveOpenText->setVisible(false);
    this->ui->progressBar->setVisible(false);

    this->doNotClose = false;

    this->ui->radioZip->setVisible(true);
    this->ui->radioFolder->setVisible(true);
    this->ui->anonPushButton->setVisible(true);

    this->ui->newDobBox->setVisible(true);
    this->ui->newNameBox->setVisible(true);
    this->ui->NewDobLabel->setVisible(true);
    this->ui->newNameLabel->setVisible(true);
}

void MainWindow::on_pushButton_clicked()
{
    QMessageBox aMessageBox;
    aMessageBox.setText("MUSIC Anonymisation tool - Copyright 2015");
    aMessageBox.exec();
}
