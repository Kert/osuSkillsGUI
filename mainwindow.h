#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLibrary>

namespace Ui {

class MainWindow;
}

#define NUM_SKILLS 7

struct RankingRawData
{
    std::string name;
    std::string mods;
    double ar;
    double cs;
    double val;
    bool operator<(const RankingRawData& other) const { return (val < other.val); }
};

struct RankingShowData
{
    std::string name;
    std::string mods;
    double ar;
    double cs;
    double val;
    QString change;
};

struct Skills
{
    double stamina = 0;
    double tenacity = 0;
    double agility = 0;
    double precision = 0;
    double reading = 0;
    double memory = 0;
    double accuracy = 0;
    double reaction = 0;
};

struct BeatmapData
{
    QString name;
    QString mods;
    double ar;
    double cs;
    Skills skills;
};

struct MapListItem
{
    QString fileName;
    QString mods;
};

enum RANKING_TYPE
{
    RANKING_STAMINA,
    RANKING_TENACITY,
    RANKING_AGILITY,
    RANKING_ACCURACY,
    RANKING_PRECISION,
    RANKING_REACTION,
    RANKING_MEMORY
};

enum MODS
{
    NF = 1,
    EZ = 2,
    HD = 8,
    HR = 16,
    SD = 32,
    DT = 64,
    RL = 128,
    HT = 256,
    FL = 1024,
    AU = 2048,
    SO = 4096,
    AP = 8192
};

class CalcThread;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButton_generate_clicked();

    void on_pushButton_load_clicked();

    void on_pushButton_save_clicked();

    void on_pushButton_calculate_clicked();

    void on_comboBox_currentIndexChanged(int index);

    void on_pushButton_selectAll_clicked();

    void on_tabWidget_tabBarClicked(int index);

    void on_tabWidget_ranking_currentChanged(int index);

    void on_pushButton_resetVars_clicked();

    void UpdateAll();

    void on_textBrowser_anchorClicked(const QUrl &arg1);

private:
    Ui::MainWindow *ui;
    QLibrary lib;
    typedef int (*FPNTR2)(void);
    FPNTR2 ReloadFormulaVars;
    CalcThread* worker;

    bool rankingCreated[NUM_SKILLS];
    bool isCalculating;
    void LoadMapListTable(const std::vector<MapListItem> &fileList);
    void UpdateOverallTable();
    void UpdateRankings();
    void ShowRanking(RANKING_TYPE skill);
    void LoadFormulaVars();
    void SaveFormulaVars();
};

class CalcThread: public QObject
{
    Q_OBJECT

public:
    CalcThread() {};
    virtual ~CalcThread() {};
    std::vector<std::pair<QString, QString>> maps;
    bool stop = false;

public slots:
    void Calculate();
    void Stop();

signals:
    void progress(int);
    void progressText(QString);
};

#endif // MAINWINDOW_H
