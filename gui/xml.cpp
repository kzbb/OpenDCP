/*
     : Builds Digital Cinema Packages
     Copyright (c) 2010-2011 Terrence Meiczinger, All Rights Reserved

     This program is free software: you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation, either version 3 of the License, or
     (at your option) any later version.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "generatetitle.h"
#include <QtGui>
#include <QDir>
#include <stdio.h>
#include <stdlib.h>
#include <opendcp.h>

asset_t pictureAsset;
asset_t soundAsset;
asset_t subtitleAsset;

void MainWindow::connectXmlSlots()
{
    // connect slots
    connect(ui->reelPictureButton, SIGNAL(clicked()), this, SLOT(setPictureTrack()));
    connect(ui->reelPictureOffsetSpinBox, SIGNAL(valueChanged(int)), SLOT(updatePictureDuration()));
    connect(ui->reelSoundButton, SIGNAL(clicked()), this, SLOT(setSoundTrack()));
    connect(ui->reelSoundOffsetSpinBox, SIGNAL(valueChanged(int)), SLOT(updateSoundDuration()));
    connect(ui->reelSubtitleButton, SIGNAL(clicked()), this, SLOT(setSubtitleTrack()));
    connect(ui->reelSubtitleOffsetSpinBox, SIGNAL(valueChanged(int)), SLOT(updateSubtitleDuration()));
    connect(ui->createDcpButton, SIGNAL(clicked()), SLOT(startDcp()));
    connect(ui->cplTitleGenButton, SIGNAL(clicked()), SLOT(getTitle()));
}

void MainWindow::getTitle() {
    if (generateTitle->exec()) {
        QString title = generateTitle->getTitle();
        ui->cplTitleEdit->setText(title);
    }
}

void MainWindow::startDcp()
{
    QString     path;
    QString     filename;
    QFileInfo   source;
    QFileInfo   destination;
    QMessageBox msgBox;
    QString     DCP_FAIL_MSG;

    asset_list_t reelList[MAX_REELS];

    opendcp_t *xmlContext = opendcp_create();

    DCP_FAIL_MSG = tr("DCP Creation Failed");

    // process options
    xmlContext->log_level = 0;

    if (ui->digitalSignatureCheckBox->checkState()) {
        xmlContext->xml_signature.sign = 1;
        xmlContext->xml_signature.use_external = 0;
    }

    dcp_log_init(xmlContext->log_level, ".log");
    strcpy(xmlContext->xml.title, ui->cplTitleEdit->text().toStdString().c_str());
    strcpy(xmlContext->xml.annotation, ui->cplAnnotationEdit->text().toStdString().c_str());
    strcpy(xmlContext->xml.issuer, ui->cplIssuerEdit->text().toStdString().c_str());
    strcpy(xmlContext->xml.kind, ui->cplKindComboBox->currentText().toStdString().c_str());
    strcpy(xmlContext->xml.rating, ui->cplRatingComboBox->currentText().toStdString().c_str());

    // check picture track is supplied
    if (ui->reelPictureEdit->text().isEmpty()) {
        QMessageBox::critical(this, DCP_FAIL_MSG,
                             tr("An MXF picture track is required."));
        goto Done;
    }

    // check durations
    if ((!ui->reelSoundEdit->text().isEmpty() && ui->reelPictureDurationSpinBox->value() != ui->reelSoundDurationSpinBox->value()) ||
        (!ui->reelSubtitleEdit->text().isEmpty() && ui->reelPictureDurationSpinBox->value() != ui->reelSubtitleDurationSpinBox->value())) {
        QMessageBox::critical(this, DCP_FAIL_MSG,
                              tr("The duration of all MXF tracks must be the same."));
        goto Done;
    }

    // copy assets
    reelList->asset_count = 0;
    if (!ui->reelPictureEdit->text().isEmpty()) {
        strcpy(reelList[0].asset_list[0].filename, ui->reelPictureEdit->text().toStdString().c_str());
        reelList->asset_count++;
    }
    if (!ui->reelSoundEdit->text().isEmpty()) {
        strcpy(reelList[0].asset_list[1].filename, ui->reelSoundEdit->text().toStdString().c_str());
        reelList->asset_count++;
    }
    if (!ui->reelSubtitleEdit->text().isEmpty()) {
        strcpy(reelList[0].asset_list[2].filename, ui->reelSubtitleEdit->text().toStdString().c_str());
        reelList->asset_count++;
    }

    // add pkl to the DCP (only one PKL currently support)
    add_pkl(xmlContext);

    // add cpl to the DCP/PKL (only one CPL currently support)
    add_cpl(xmlContext, &xmlContext->pkl[0]);

    if (add_reel(xmlContext, &xmlContext->pkl[0].cpl[0],reelList[0]) != OPENDCP_NO_ERROR) {
        QMessageBox::critical(this, DCP_FAIL_MSG,
                              tr("Could not add reel to CPL."));
        goto Done;
    }

    // adjust durations
    xmlContext->pkl[0].cpl[0].reel[0].asset[0].duration = ui->reelPictureDurationSpinBox->value();
    xmlContext->pkl[0].cpl[0].reel[0].asset[0].entry_point = ui->reelPictureOffsetSpinBox->value();
    xmlContext->pkl[0].cpl[0].reel[0].asset[1].duration = ui->reelSoundDurationSpinBox->value();
    xmlContext->pkl[0].cpl[0].reel[0].asset[1].entry_point = ui->reelSoundOffsetSpinBox->value();
    xmlContext->pkl[0].cpl[0].reel[0].asset[2].duration = ui->reelSubtitleDurationSpinBox->value();
    xmlContext->pkl[0].cpl[0].reel[0].asset[2].entry_point = ui->reelSubtitleOffsetSpinBox->value();

    if (validate_reel(xmlContext,&xmlContext->pkl[0].cpl[0],0) != OPENDCP_NO_ERROR) {
        QMessageBox::critical(this, DCP_FAIL_MSG,
                              tr("Could not valiate reel."));
        goto Done;
    }

    // set filenames
    path = QFileDialog::getExistingDirectory(this, tr("Choose destination folder"),QString::null);

    if (path.isEmpty()) {
        goto Done;
    }

    filename = path + "/" + xmlContext->pkl[0].cpl[0].uuid + "_cpl.xml";
    strcpy(xmlContext->pkl[0].cpl[0].filename,filename.toStdString().c_str());
    filename = path + "/" + xmlContext->pkl[0].uuid + "_pkl.xml";
    strcpy(xmlContext->pkl[0].filename,filename.toStdString().c_str());
    if (xmlContext->ns == XML_NS_SMPTE) {
        filename = path + "/" + "ASSETMAP.xml";
        strcpy(xmlContext->assetmap.filename,filename.toStdString().c_str());
        filename = path + "/" + "VOLINDEX.xml";
        strcpy(xmlContext->volindex.filename,filename.toStdString().c_str());
    } else {
        filename = path + "/" + "ASSETMAP";
        strcpy(xmlContext->assetmap.filename,filename.toStdString().c_str());
        filename = path + "/" + "VOLINDEX";
        strcpy(xmlContext->volindex.filename,filename.toStdString().c_str());
    }

    // write XML Files
    if (write_cpl(xmlContext,&xmlContext->pkl[0].cpl[0]) != OPENDCP_NO_ERROR) {
        QMessageBox::critical(this, DCP_FAIL_MSG,
                              tr("Failed to create composition playlist."));
        goto Done;
    }
    if (write_pkl(xmlContext,&xmlContext->pkl[0]) != OPENDCP_NO_ERROR) {
        QMessageBox::critical(this, DCP_FAIL_MSG,
                              tr("Failed to create packaging list."));
        goto Done;
    }
    if (write_volumeindex(xmlContext) != OPENDCP_NO_ERROR) {
        QMessageBox::critical(this, DCP_FAIL_MSG,
                              tr("Failed to create volume index."));
        goto Done;
    }
    if (write_assetmap(xmlContext) != OPENDCP_NO_ERROR) {
        QMessageBox::critical(this, DCP_FAIL_MSG,
                              tr("Failed to create assetmap."));
        goto Done;
    }

    // copy the picture mxf files
    source.setFile(xmlContext->pkl[0].cpl[0].reel[0].asset[0].filename);
    destination.setFile(path + "/" + source.fileName());

    if (!ui->reelPictureEdit->text().isEmpty() && source.absoluteFilePath() != destination.absoluteFilePath()) {
        if (destination.isFile()) {
            if (QMessageBox::question(this,tr("Move MXF File"),tr("The destination picture MXF already exists, do you want to replace?"),
                                      QMessageBox::No,QMessageBox::Yes) == QMessageBox::Yes) {
                QFile::remove(destination.absoluteFilePath());
            }
        }
        if (ui->rbMoveMxf->isChecked()) {
            QFile::rename(source.absoluteFilePath(), destination.absoluteFilePath());
        } else {
            //QMessageBox* msgBox = new QMessageBox( this );
            //msgBox->setWindowTitle("File Copy");
            //msgBox->setText("Copying picture file...");
            //msgBox->open();
            fileCopy(source.absoluteFilePath(), destination.absoluteFilePath());
            //msgBox->close();;
        }
    }

    // copy the sound mxf files
    source.setFile(xmlContext->pkl[0].cpl[0].reel[0].asset[1].filename);
    destination.setFile(path + "/" + source.fileName());
    if (!ui->reelSoundEdit->text().isEmpty() && source.absoluteFilePath() != destination.absoluteFilePath()) {
        if (destination.isFile()) {
            if (QMessageBox::question(this,tr("Move MXF File"),tr("The destination sound MXF already exists, do you want to replace?"),
                                      QMessageBox::No,QMessageBox::Yes) == QMessageBox::Yes) {
                QFile::remove(destination.absoluteFilePath());
            }
        }
        if (ui->rbMoveMxf->isChecked()) {
            QFile::rename(source.absoluteFilePath(), destination.absoluteFilePath());
        } else {
            QMessageBox* msgBox = new QMessageBox( this );
            //msgBox->setAttribute( QWidget::WA_DeleteOnClose );
            msgBox->setWindowTitle("File Copy");
            msgBox->setText("Copying sound file...");
            msgBox->open();
            QFile::copy(source.absoluteFilePath(), destination.absoluteFilePath());
            msgBox->close();
        }
    }

    // copy the subtitle mxf files
    source.setFile(xmlContext->pkl[0].cpl[0].reel[0].asset[2].filename);
    destination.setFile(path + "/" + source.fileName());
    if (!ui->reelSubtitleEdit->text().isEmpty() && source.absoluteFilePath() != destination.absoluteFilePath()) {
        if (destination.isFile()) {
            if (QMessageBox::question(this,tr("Move MXF File"),tr("The destination subtitle MXF already exists, do you want to replace?"),
                                      QMessageBox::No,QMessageBox::Yes) == QMessageBox::Yes) {
                QFile::remove(destination.absoluteFilePath());
            }
        }
        if (ui->rbMoveMxf->isChecked()) {
            QFile::rename(source.absoluteFilePath(), destination.absoluteFilePath());
        } else {
            QMessageBox* msgBox = new QMessageBox( this );
            msgBox->setWindowTitle("File Copy");
            msgBox->setText("Copying subtitle file...");
            msgBox->open();
            QFile::copy(source.absoluteFilePath(), destination.absoluteFilePath());
            msgBox->close();;
        }
    }

    msgBox.setText("DCP Created successfully");
    msgBox.exec();


Done:
    opendcp_delete(xmlContext);

    return;
}

void MainWindow::updatePictureDuration()
{
    int offset = ui->reelPictureOffsetSpinBox->value();
    ui->reelPictureDurationSpinBox->setMaximum(pictureAsset.intrinsic_duration-offset);
}

void MainWindow::updateSoundDuration()
{
    int offset = ui->reelSoundOffsetSpinBox->value();
    ui->reelSoundDurationSpinBox->setMaximum(soundAsset.intrinsic_duration-offset);
}

void MainWindow::updateSubtitleDuration()
{
    int offset = ui->reelSubtitleOffsetSpinBox->value();
    ui->reelSubtitleDurationSpinBox->setMaximum(subtitleAsset.intrinsic_duration-offset);
}

void MainWindow::setPictureTrack()
{
    QString path;
    QString filter = "*.mxf";
    char *file;

    path = QFileDialog::getOpenFileName(this, tr("Choose a file to open"),QString::null,filter);

    if (path.isEmpty()) {
        return;
    }

    file = new char [path.toStdString().size()+1];
    strcpy(file, path.toStdString().c_str());
    if (get_file_essence_class(file) != ACT_PICTURE) {
        QMessageBox::critical(this, tr("Not a Picture Track"),
                              tr("The selected file is not a valid MXF picture track."));
    } else {
        ui->reelPictureEdit->setProperty("text", path);
        strcpy(pictureAsset.filename, ui->reelPictureEdit->text().toStdString().c_str());
        read_asset_info(&pictureAsset);
        ui->reelPictureDurationSpinBox->setMaximum(pictureAsset.intrinsic_duration);
        ui->reelPictureOffsetSpinBox->setMaximum(pictureAsset.intrinsic_duration-1);
        ui->reelPictureDurationSpinBox->setValue(pictureAsset.duration);
    }

    delete[] file;

    return;
}

void MainWindow::setSoundTrack()
{
    QString path;
    QString filter = "*.mxf";
    char *file;

    path = QFileDialog::getOpenFileName(this, tr("Choose a file to open"),QString::null,filter);

    if (path.isEmpty()) {
        return;
    }

    file = new char [path.toStdString().size()+1];
    strcpy(file, path.toStdString().c_str());
    if (get_file_essence_class(file) != ACT_SOUND) {
        QMessageBox::critical(this, tr("Not a Sound Track"),
                             tr("The selected file is not a valid MXF sound track."));
    } else {
        ui->reelSoundEdit->setProperty("text", path);
        strcpy(soundAsset.filename, ui->reelSoundEdit->text().toStdString().c_str());
        read_asset_info(&soundAsset);
        ui->reelSoundDurationSpinBox->setValue(soundAsset.duration);
        ui->reelSoundDurationSpinBox->setMaximum(soundAsset.intrinsic_duration);
        ui->reelSoundOffsetSpinBox->setMaximum(soundAsset.intrinsic_duration-1);
    }

    delete[] file;

    return;
}

void MainWindow::setSubtitleTrack()
{
    QString path;
    QString filter = "*.mxf";
    char *file;

    path = QFileDialog::getOpenFileName(this, tr("Choose an file to open"),QString::null,filter);

    if (path.isEmpty()) {
        return;
    }

    file = new char [path.toStdString().size()+1];
    strcpy(file, path.toStdString().c_str());
    if (get_file_essence_class(file) != ACT_TIMED_TEXT) {
        QMessageBox::critical(this, tr("Not a Subtitle Track"),
                              tr("The selected file is not a valid MXF subtitle track."));
    } else {
        ui->reelSubtitleEdit->setProperty("text", path);
        strcpy(subtitleAsset.filename, ui->reelSubtitleEdit->text().toStdString().c_str());
        read_asset_info(&subtitleAsset);
        ui->reelSubtitleDurationSpinBox->setValue(subtitleAsset.duration);
        ui->reelSubtitleDurationSpinBox->setMaximum(subtitleAsset.intrinsic_duration);
        ui->reelSubtitleOffsetSpinBox->setMaximum(subtitleAsset.intrinsic_duration-1);
    }

    delete[] file;

    return;
}
