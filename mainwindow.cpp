#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDesktopServices>
#include <QDirIterator>
#include <QFileDialog>
#include <QLibrary>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QStandardItemModel>
#include <QTextStream>
#include <QThread>

static std::vector<std::vector<RankingRawData>> rankingRawCurrent(NUM_SKILLS, std::vector<RankingRawData>());
static std::vector<std::vector<RankingRawData>> rankingRawPrevious(NUM_SKILLS, std::vector<RankingRawData>());
static std::vector<std::vector<RankingShowData>> rankingShow(NUM_SKILLS, std::vector<RankingShowData>());
static QString configPath;
static std::vector<BeatmapData> beatmapSkills;
typedef int (*FPNTR)(std::string, int&, int&, int mods, Skills &skills, std::string &name, double &ar, double &cs);
static FPNTR CalculateBeatmapSkills;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QString dllPath = QDir::currentPath()+"/osuSkills.dll";
    qApp->addLibraryPath(dllPath);
    if(QLibrary::isLibrary(dllPath))
    {
        lib.setFileName(dllPath);
        lib.load();
        if(!lib.isLoaded())
            QMessageBox::critical(this, tr("osuSkillsGUI"), tr("Can't load osuSkills dll file from ") + dllPath);
    }
    else
        QMessageBox::critical(this, tr("osuSkillsGUI"), dllPath + tr(" is not a valid osuSkills dll file"));

    CalculateBeatmapSkills = reinterpret_cast<FPNTR>(lib.resolve("CalculateBeatmapSkills"));
    if(!CalculateBeatmapSkills)
        QMessageBox::critical(this, tr("osuSkillsGUI"), tr("Could not find CalculateBeatmapSkills in dll ") + dllPath);
    ReloadFormulaVars = reinterpret_cast<FPNTR2>(lib.resolve("ReloadFormulaVars"));
    if(!ReloadFormulaVars)
        QMessageBox::critical(this, tr("osuSkillsGUI"), tr("Could not find ReloadFormulaVars in dll ") + dllPath);

    configPath = QDir::currentPath()+"/config.cfg";

    ReloadFormulaVars();
    LoadFormulaVars();

    ui->tableWidget_mapList->setColumnWidth(0,645);
    ui->tableWidget_mapList->setColumnWidth(1,100);
    isCalculating = false;
}

MainWindow::~MainWindow()
{
    if(lib.isLoaded())
       lib.unload();
    delete ui;
}

void MainWindow::LoadFormulaVars()
{
    QSettings config(configPath, QSettings::IniFormat);
    QStringList skills = config.childGroups();
    foreach (const QString &childKey, skills)
    {
        QWidget *newTab = new QWidget(ui->tabWidget_configVars);
        ui->tabWidget_configVars->addTab(newTab, childKey);
        config.beginGroup(childKey);

        QTableWidget *table = new QTableWidget(newTab);
        table->setMinimumWidth(ui->tabWidget_configVars->width());
        table->verticalHeader()->setVisible(0);
        table->insertRow(0);
        table->setVisible(true);

        int column = 0;
        const QStringList skillVars = config.childKeys();
        foreach (const QString &var, skillVars)
        {
            table->insertColumn(table->columnCount());
            table->setHorizontalHeaderItem(column, new QTableWidgetItem(var));
            table->setItem(0,column, new QTableWidgetItem(config.value(var).toString()));
            column++;
        }
        table->resizeColumnsToContents();

        config.endGroup();
    }
}

void MainWindow::SaveFormulaVars()
{
    QSettings config(configPath, QSettings::IniFormat);

    for(int i = 0; i < ui->tabWidget_configVars->count(); i++)
    {
        QString skillName = ui->tabWidget_configVars->tabText(i);
        QWidget *skillWidget = ui->tabWidget_configVars->widget(i);

        // extremely sure that there is only one child that is QTableWidget
        QTableWidget *variable = static_cast<QTableWidget*>(skillWidget->children().at(0));

        config.beginGroup(skillName);
        // saving each variable
        for(int j = 0; j < variable->columnCount(); j++)
        {
            QString varName = variable->horizontalHeaderItem(j)->text();
            double val = variable->item(0,j)->text().toDouble();
            config.setValue(varName, val);
        }
        config.endGroup();
    }
    config.sync();
}

void MainWindow::on_pushButton_resetVars_clicked()
{
    QFile file(configPath);
    file.remove();
    ReloadFormulaVars();
    while(ui->tabWidget_configVars->widget(0))
        delete ui->tabWidget_configVars->widget(0);
    LoadFormulaVars();
}

void MainWindow::LoadMapListTable(const std::vector<MapListItem> &fileList)
{
    ui->tableWidget_mapList->setRowCount(0);
    for(auto &map : fileList)
    {
        ui->tableWidget_mapList->insertRow(ui->tableWidget_mapList->rowCount());
        ui->tableWidget_mapList->setItem(ui->tableWidget_mapList->rowCount()-1,
                                 0,
                                 new QTableWidgetItem(map.fileName));
        ui->tableWidget_mapList->setItem(ui->tableWidget_mapList->rowCount()-1,
                                 1,
                                 new QTableWidgetItem(map.mods));
    }
    this->ui->tableWidget_mapList->selectAll();
    this->ui->tableWidget_mapList->setFocus();
}

void MainWindow::on_pushButton_generate_clicked()
{
    QString filePath = QFileDialog::getExistingDirectory(this,tr("Choose folder"));
    if(!filePath.length())
        return;

    std::string filePa = filePath.toStdString();
    std::vector<QString> fileList;

    QDirIterator it(filePath, QStringList() << "*.osu", QDir::Files, QDirIterator::Subdirectories);
    std::vector<MapListItem> mapList;
    while (it.hasNext())
    {
        QString map = it.next();
        fileList.push_back(map);
        mapList.push_back(MapListItem{map, ""});
    }
    LoadMapListTable(mapList);
}

void MainWindow::on_pushButton_load_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this,tr("Choose map list"), QDir::currentPath(), "Text File (*.txt)");
    if(!filePath.size())
        return;
    // load from file
    std::vector<MapListItem> fileList;
    QFile inputFile(filePath);
    if (inputFile.open(QIODevice::ReadOnly))
    {
        QTextStream in(&inputFile);
        while (!in.atEnd())
        {
            QString line = in.readLine();
            if (!line.length())   continue;
            if (line.contains("//")) continue; // ignore commented maps

            QString mods = "";
            QStringList tokens = line.split('\"');
            if(tokens.size() > 1) // there are mods!
            {
                if(tokens[2].length())
                    mods = tokens[2];
                line = tokens[1];
            }
            fileList.push_back(MapListItem{line,mods});
        }
        inputFile.close();
    }
    else
    {
        QMessageBox::critical(this, tr("osuSkillsGUI"), tr("Could not read Map List file ") + filePath);
        return;
    }

    LoadMapListTable(fileList);
}

void MainWindow::on_pushButton_save_clicked()
{
    QString filePath = QFileDialog::getSaveFileName(this,tr("Choose map list"), QDir::currentPath(), "Text File (*.txt)");
    // save to file
    if(!filePath.size())
        return;
    QFile outputFile(filePath);
    if (outputFile.open(QIODevice::WriteOnly))
    {
        QTextStream out(&outputFile);
        for(int i = 0; i < ui->tableWidget_mapList->rowCount(); i++)
            out << "\"" << ui->tableWidget_mapList->item(i,0)->text() << "\"" << ui->tableWidget_mapList->item(i,1)->text() <<  endl;
        outputFile.close();
    }
    else
    {
        QMessageBox::critical(this, tr("osuSkillsGUI"), tr("Could not save Map List file ") + filePath);
        return;
    }
}

void MainWindow::UpdateOverallTable()
{
    if(ui->tableView_overallTable->model() != nullptr)
            ui->tableView_overallTable->model()->removeColumns(0,ui->tableView_overallTable->model()->columnCount());

    QStandardItemModel *model = new QStandardItemModel(1,11,this);
    model->setHorizontalHeaderItem(0, new QStandardItem(QString("Map")));
    model->setHorizontalHeaderItem(1, new QStandardItem(QString("Mods")));
    model->setHorizontalHeaderItem(2, new QStandardItem(QString("AR")));
    model->setHorizontalHeaderItem(3, new QStandardItem(QString("CS")));
    model->setHorizontalHeaderItem(4, new QStandardItem(QString("Sta")));
    model->setHorizontalHeaderItem(5, new QStandardItem(QString("Ten")));
    model->setHorizontalHeaderItem(6, new QStandardItem(QString("Agi")));
    model->setHorizontalHeaderItem(7, new QStandardItem(QString("Acc")));
    model->setHorizontalHeaderItem(8, new QStandardItem(QString("Pre")));
    model->setHorizontalHeaderItem(9, new QStandardItem(QString("Reac")));
    model->setHorizontalHeaderItem(10, new QStandardItem(QString("Mem")));
    model->setRowCount(static_cast<int>(beatmapSkills.size()));
    ui->tableView_overallTable->setModel(model);
    ui->tableView_overallTable->setColumnWidth(0, 350);
    ui->tableView_overallTable->setColumnWidth(1, 100);
    ui->tableView_overallTable->setColumnWidth(2, 20);
    ui->tableView_overallTable->setColumnWidth(3, 20);
    for(int i = 4; i < 11; i++)
    {
        ui->tableView_overallTable->setColumnWidth(i, 40);
    }
    int rowIndex = 0;
    for(auto &map : beatmapSkills)
    {
        model->setItem(rowIndex, 0, new QStandardItem(map.name));
        model->setItem(rowIndex, 1, new QStandardItem(map.mods));
        QStandardItem *item = new QStandardItem();
        item->setData(QString::number(map.ar, 'g', 2), Qt::DisplayRole);
        model->setItem(rowIndex, 2, item);
        item = new QStandardItem();
        item->setData(QString::number(map.cs, 'g', 2), Qt::DisplayRole);
        model->setItem(rowIndex, 3, item);
        item = new QStandardItem();
        item->setData(static_cast<int>(map.skills.stamina), Qt::DisplayRole);
        model->setItem(rowIndex, 4, item);
        item = new QStandardItem();
        item->setData(static_cast<int>(map.skills.tenacity), Qt::DisplayRole);
        model->setItem(rowIndex, 5, item);
        item = new QStandardItem();
        item->setData(static_cast<int>(map.skills.agility), Qt::DisplayRole);
        model->setItem(rowIndex, 6, item);
        item = new QStandardItem();
        item->setData(static_cast<int>(map.skills.accuracy), Qt::DisplayRole);
        model->setItem(rowIndex, 7, item);
        item = new QStandardItem();
        item->setData(static_cast<int>(map.skills.precision), Qt::DisplayRole);
        model->setItem(rowIndex, 8, item);
        item = new QStandardItem();
        item->setData(static_cast<int>(map.skills.reaction), Qt::DisplayRole);
        model->setItem(rowIndex, 9, item);
        item = new QStandardItem();
        item->setData(static_cast<int>(map.skills.memory), Qt::DisplayRole);
        model->setItem(rowIndex, 10, item);
        rowIndex++;
    }
    ui->tableView_overallTable->setSortingEnabled(true);
}

void MainWindow::UpdateRankings()
{
    for (unsigned i = 0; i < rankingRawCurrent.size(); i++)
    {
            rankingRawCurrent[i].clear();
            rankingShow[i].clear();
    }

    for(auto &map : beatmapSkills)
    {
        RankingRawData data;
        data.name = map.name.toStdString();
        data.mods = map.mods.toStdString();
        data.ar = map.ar;
        data.cs = map.cs;
        data.val = map.skills.stamina;
        rankingRawCurrent.at(RANKING_STAMINA).push_back(data);
        data.val = map.skills.tenacity;
        rankingRawCurrent.at(RANKING_TENACITY).push_back(data);
        data.val = map.skills.agility;
        rankingRawCurrent.at(RANKING_AGILITY).push_back(data);
        data.val = map.skills.accuracy;
        rankingRawCurrent.at(RANKING_ACCURACY).push_back(data);
        data.val = map.skills.precision;
        rankingRawCurrent.at(RANKING_PRECISION).push_back(data);
        data.val = map.skills.reaction;
        rankingRawCurrent.at(RANKING_REACTION).push_back(data);
        data.val = map.skills.memory;
        rankingRawCurrent.at(RANKING_MEMORY).push_back(data);
    }

    for (unsigned type = 0; type < rankingRawCurrent.size(); type++)
    {
        std::sort(rankingRawCurrent[type].rbegin(), rankingRawCurrent[type].rend()); // for quicker chart making
        for (unsigned i = 0; i < rankingRawCurrent[type].size(); i++)
        {
            RankingRawData record = rankingRawCurrent[type][i];
            std::string changeStr;
            for (unsigned j = 0; j < rankingRawPrevious[type].size(); j++)
            {
                if (!rankingRawPrevious[type][j].name.compare(0, record.name.length(), record.name) && !rankingRawPrevious[type][j].mods.compare(0, record.mods.length(), record.mods))
                {
                    int changeRank = 0;
                    double changeVal = 0;
                    std::string sign = "";
                    changeRank = static_cast<int>(j) - static_cast<int>(i);
                    if (changeRank >= 0)
                        sign = "+";
                    changeRank = abs(changeRank);
                    changeStr = sign + std::to_string(changeRank);
                    changeVal = record.val - rankingRawPrevious[type][j].val;
                    if (changeVal >= 0)
                        sign = "+";
                    else
                        sign ="";
                    changeStr = "(" + sign + std::to_string(static_cast<int>(changeVal)) + ") " + changeStr;
                    break;
                }
            }
            RankingShowData show;
            show.val = record.val;
            show.name = record.name;
            show.change = tr(changeStr.c_str());
            show.mods = record.mods;
            show.ar = record.ar;
            show.cs = record.cs;
            rankingShow.at(type).push_back(show);
            //logFile << record.val << "\t[" << sign << change << "]\t(" << record.cs << " CS)\t(" << record.ar << " AR)\t" << record.name << endl;
        }
        rankingRawPrevious[type] = rankingRawCurrent[type];
    }
}

void CalcThread::Stop()
{
    stop = true;
}

void CalcThread::Calculate()
{
    unsigned totalSelectedMaps = static_cast<unsigned>(maps.size());
    int countProcessed = 0;
    for (unsigned i = 0; i < totalSelectedMaps; i++)
    {
        if(stop)
            break;
        QString mapFileName = maps[i].first;
        QString modString = maps[i].second;
        int mods = 0;
        if (modString.length()) // there are some mods here probably!
        {
            QStringList tokensMods = modString.split(" +");
            if (tokensMods.size())
            {
                for (auto mod : tokensMods)
                {
                    if (!QString::compare(mod, "EZ", Qt::CaseInsensitive))
                        mods += MODS::EZ;
                    if (!QString::compare(mod, "HT", Qt::CaseInsensitive))
                        mods += MODS::HT;
                    if (!QString::compare(mod, "HR", Qt::CaseInsensitive))
                        mods += MODS::HR;
                    if (!QString::compare(mod, "DT", Qt::CaseInsensitive))
                        mods += MODS::DT;
                    if (!QString::compare(mod, "HD", Qt::CaseInsensitive))
                        mods += MODS::HD;
                    if (!QString::compare(mod, "FL", Qt::CaseInsensitive))
                        mods += MODS::FL;
                }
            }
        }
        Skills skills;
        int unused = 0;
        double ar, cs;

        emit progressText(mapFileName);
        std::string beatmapName;
        int res = CalculateBeatmapSkills(mapFileName.toStdString(), unused, unused, mods, skills, beatmapName, ar, cs);
        if(res) // if calc is successful
        {
            BeatmapData beatmapData;
            beatmapData.name = tr(beatmapName.c_str());
            beatmapData.skills = skills;
            beatmapData.mods = modString;
            beatmapData.ar = ar;
            beatmapData.cs = cs;
            beatmapSkills.push_back(beatmapData);            
        }
        countProcessed++;
        emit progress(countProcessed);
    }
    this->thread()->quit();
}

void MainWindow::UpdateAll()
{
    ui->label_mapProcessingName->setText("none");

    for(unsigned i = 0; i < beatmapSkills.size(); i++)
    {
        BeatmapData *map = &beatmapSkills[i];
        ui->comboBox->addItem(map->name + map->mods);
    }
    UpdateOverallTable();
    UpdateRankings();
    ui->pushButton_calculate->setText("Calculate");
    isCalculating = false;
    ui->label_mapProcessingName->setText("none");
}

void MainWindow::on_pushButton_calculate_clicked()
{
    if(isCalculating)
    {
        worker->Stop();
        ui->pushButton_calculate->setText("Calculate");
        isCalculating = false;
        ui->label_mapProcessingName->setText("none");
        return;
    }
    if(!ui->tableWidget_mapList->rowCount())
        return;

    SaveFormulaVars();
    ReloadFormulaVars();

    for(int i = 0; i < NUM_SKILLS; i++)
        rankingCreated[i] = false;
    ui->comboBox->clear();
    beatmapSkills.clear();

    std::vector<std::pair<QString, QString>> maps;
    for (int i = 0; i < ui->tableWidget_mapList->rowCount(); i++)
    {
        QTableWidgetItem *mapNameItem = ui->tableWidget_mapList->item(i,0);
        if(!mapNameItem->isSelected())
            continue;
        std::pair<QString, QString> map;
        map.first = mapNameItem->text();
        QTableWidgetItem *modsItem = ui->tableWidget_mapList->item(i,1);
        map.second = modsItem->text();
        maps.push_back(map);
    }

    ui->progressBar->setRange(0, static_cast<int>(maps.size()));

    QThread *thread = new QThread(this);
    worker = new CalcThread;
    worker->moveToThread(thread);
    worker->maps = maps;

    connect(thread, SIGNAL(finished()), worker, SLOT(deleteLater()));
    connect(thread, SIGNAL(finished()), this, SLOT(UpdateAll()));
    connect(thread, SIGNAL(started()), worker, SLOT(Calculate()));

    connect(worker, SIGNAL(progress(int)), ui->progressBar, SLOT(setValue(int)));
    connect(worker, SIGNAL(progressText(QString)), ui->label_mapProcessingName, SLOT(setText(QString)));
    thread->start();

    isCalculating = true;
    ui->pushButton_calculate->setText("Stop");
}

void MainWindow::on_comboBox_currentIndexChanged(int index)
{
    int comboBoxIndex = this->ui->comboBox->currentIndex();
    if(comboBoxIndex < 0)
        return;

    unsigned mapIndex = static_cast<unsigned>(comboBoxIndex);
    this->ui->lineEdit_mapStamina->setText(QString::number(static_cast<int>(beatmapSkills[mapIndex].skills.stamina)));
    this->ui->lineEdit_mapTenacity->setText(QString::number(static_cast<int>(beatmapSkills[mapIndex].skills.tenacity)));
    this->ui->lineEdit_mapAgility->setText(QString::number(static_cast<int>(beatmapSkills[mapIndex].skills.agility)));
    this->ui->lineEdit_mapAccuracy->setText(QString::number(static_cast<int>(beatmapSkills[mapIndex].skills.accuracy)));
    this->ui->lineEdit_mapPrecision->setText(QString::number(static_cast<int>(beatmapSkills[mapIndex].skills.precision)));
    this->ui->lineEdit_mapReaction->setText(QString::number(static_cast<int>(beatmapSkills[mapIndex].skills.reaction)));
    this->ui->lineEdit_mapMemory->setText(QString::number(static_cast<int>(beatmapSkills[mapIndex].skills.memory)));
}

void MainWindow::on_pushButton_selectAll_clicked()
{
    this->ui->tableWidget_mapList->selectAll();
}

void MainWindow::ShowRanking(RANKING_TYPE skill)
{
    if(rankingCreated[skill])
        return;
    QString skillStr;
    QTableView *table = nullptr;
    switch(skill)
    {
        case RANKING_STAMINA:
            skillStr = "Stamina";
            table = ui->tableView_stamina;
        break;
        case RANKING_TENACITY:
            skillStr = "Tenacity";
            table = ui->tableView_tenacity;
        break;
        case RANKING_AGILITY:
            skillStr = "Agility";
            table = ui->tableView_agility;
        break;
        case RANKING_ACCURACY:
            skillStr = "Accuracy";
            table = ui->tableView_accuracy;
        break;
        case RANKING_PRECISION:
            skillStr = "Precision";
            table = ui->tableView_precision;
        break;
        case RANKING_REACTION:
            skillStr = "Reaction";
            table = ui->tableView_reaction;
        break;
        case RANKING_MEMORY:
            skillStr = "Memory";
            table = ui->tableView_memory;
        break;
    }

    // clearing
    if(table->model() != nullptr)
            table->model()->removeColumns(0,table->model()->columnCount());

    QStandardItemModel *model = new QStandardItemModel(1,6,this);
    model->setHorizontalHeaderItem(0, new QStandardItem(QString("Map")));
    model->setHorizontalHeaderItem(1, new QStandardItem(QString("Mods")));
    model->setHorizontalHeaderItem(2, new QStandardItem(QString("AR")));
    model->setHorizontalHeaderItem(3, new QStandardItem(QString("CS")));
    model->setHorizontalHeaderItem(4, new QStandardItem(skillStr));
    model->setHorizontalHeaderItem(5, new QStandardItem(QString("Change")));
    model->setRowCount(static_cast<int>(beatmapSkills.size()));
    delete table->model();
    table->setModel(model);
    table->setColumnWidth(0, 470);
    table->setColumnWidth(1, 100);
    table->setColumnWidth(2, 20);
    table->setColumnWidth(3, 20);
    table->setColumnWidth(4, 60);
    table->setColumnWidth(5, 80);

    int rowIndex = 0;
    for (unsigned i = 0; i < rankingShow[skill].size(); i++)
    {
        RankingShowData data = rankingShow[skill][i];
        model->setItem(rowIndex, 0, new QStandardItem(data.name.c_str()));
        model->setItem(rowIndex, 1, new QStandardItem(data.mods.c_str()));
        QStandardItem *item = new QStandardItem();        
        item->setData(QString::number(data.ar, 'g', 2), Qt::DisplayRole);
        model->setItem(rowIndex, 2, item);
        item = new QStandardItem();
        item->setData(QString::number(data.cs, 'g', 2), Qt::DisplayRole);
        model->setItem(rowIndex, 3, item);
        item = new QStandardItem();
        item->setData(static_cast<int>(data.val), Qt::DisplayRole);
        model->setItem(rowIndex, 4, item);
        QStandardItem *changeItem = new QStandardItem(data.change);
        changeItem->setToolTip("(+points) +rank");
        model->setItem(rowIndex, 5, changeItem);

        rowIndex++;
    }

    table->sortByColumn(4, Qt::SortOrder::DescendingOrder);
    table->setSortingEnabled(true);
    rankingCreated[skill] = true;
}

void MainWindow::on_tabWidget_tabBarClicked(int index)
{
    if(index != 1)
        return;
    ui->tabWidget_ranking->tabBar()->currentChanged(ui->tabWidget_ranking->currentIndex());
}

void MainWindow::on_tabWidget_ranking_currentChanged(int index)
{
    ShowRanking(static_cast<RANKING_TYPE>(index));
}

void MainWindow::on_textBrowser_anchorClicked(const QUrl &arg1)
{
    QDesktopServices::openUrl(QUrl(arg1));
}
