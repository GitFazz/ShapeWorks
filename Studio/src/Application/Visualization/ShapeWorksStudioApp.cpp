// std
#include <iostream>

// qt
#include <QFileDialog>
#include <QWidgetAction>
#include <QMessageBox>
#include <QCloseEvent>
#include <QTextStream>
#include <QMimeData>

// vtk
#include <vtkRenderWindow.h>
#include <vtkPolyDataWriter.h>

// shapeworks
#include <Applications/Configuration.h>
#include <Libs/Utils/StringUtils.h>
#include <Libs/Mesh/Mesh.h>

// studio
#include <Data/Preferences.h>
#include <Groom/GroomTool.h>
#include <Optimize/OptimizeTool.h>
#include <Analysis/AnalysisTool.h>
#include <Data/Session.h>
#include <Data/Shape.h>
#include <Data/StudioMesh.h>
#include <Data/StudioLog.h>
#include <Visualization/ShapeWorksStudioApp.h>
#include <Visualization/Lightbox.h>
#include <Visualization/Visualizer.h>
#include <Visualization/WheelEventForwarder.h>
#include <Interface/SplashScreen.h>
#include <Interface/KeyboardShortcuts.h>

// ui
#include <ui_ShapeWorksStudioApp.h>

namespace shapeworks {

static QVariant ITEM_DISABLE(0);
static QVariant ITEM_ENABLE(1 | 32);
static int ITEM_ROLE = Qt::UserRole - 1;

const std::string ShapeWorksStudioApp::SETTING_ZOOM_C("zoom_state");

//---------------------------------------------------------------------------
ShapeWorksStudioApp::ShapeWorksStudioApp()
{
  this->ui_ = new Ui_ShapeWorksStudioApp;
  this->ui_->setupUi(this);
  this->setAcceptDrops(true);
  this->progress_bar_ = new QProgressBar(this);
  this->ui_->statusbar->addPermanentWidget(this->progress_bar_);
  this->progress_bar_->setVisible(false);

  // default hide
  this->ui_->feature_widget->hide();
  this->recent_file_actions_.append(this->ui_->action_recent1);
  this->recent_file_actions_.append(this->ui_->action_recent2);
  this->recent_file_actions_.append(this->ui_->action_recent3);
  this->recent_file_actions_.append(this->ui_->action_recent4);

  for (int i = 0; i < 4; i++) {
    connect(this->recent_file_actions_[i], SIGNAL(triggered()),
            this, SLOT(handle_open_recent()));
  }
  this->update_recent_files();

  this->splash_screen_ = QSharedPointer<SplashScreen>(new SplashScreen(this, this->preferences_));
  connect(this->splash_screen_.data(), &SplashScreen::open_project, this,
          &ShapeWorksStudioApp::open_project);

  this->wheel_event_forwarder_ = QSharedPointer<WheelEventForwarder>
    (new WheelEventForwarder(this->ui_->vertical_scroll_bar));
  this->ui_->qvtkWidget->installEventFilter(this->wheel_event_forwarder_.data());

  // set the splitter ratio
  this->ui_->data_splitter->setSizes(QList<int>({INT_MAX, INT_MAX}));

  // Glyph options in the render window.
  QMenu* menu = new QMenu();
  QWidget* widget = new QWidget();
  QGridLayout* layout = new QGridLayout(widget);

  QLabel* size_label = new QLabel("Glyph Size: ");
  layout->addWidget(size_label, 0, 0, 1, 1);
  size_label = new QLabel("Glyph Detail: ");
  layout->addWidget(size_label, 1, 0, 1, 1);

  this->glyph_quality_label_ = new QLabel("....");
  this->glyph_quality_label_->setMinimumWidth(50);
  this->glyph_size_label_ = new QLabel("....");
  this->glyph_size_label_->setMinimumWidth(50);
  layout->addWidget(this->glyph_size_label_, 0, 1, 1, 1);
  layout->addWidget(this->glyph_quality_label_, 1, 1, 1, 1);

  this->glyph_size_slider_ = new QSlider(widget);
  this->glyph_size_slider_->setOrientation(Qt::Horizontal);
  this->glyph_size_slider_->setMinimum(1);
  this->glyph_size_slider_->setMaximum(100);
  this->glyph_size_slider_->setPageStep(10);
  this->glyph_size_slider_->setTickPosition(QSlider::TicksBelow);
  this->glyph_size_slider_->setTickInterval(10);
  this->glyph_size_slider_->setMinimumWidth(200);

  this->glyph_auto_size_ = new QCheckBox("Auto");

  this->glyph_quality_slider_ = new QSlider(widget);
  this->glyph_quality_slider_->setMinimum(1);
  this->glyph_quality_slider_->setMaximum(20);
  this->glyph_quality_slider_->setPageStep(3);
  this->glyph_quality_slider_->setOrientation(Qt::Horizontal);
  this->glyph_quality_slider_->setTickPosition(QSlider::TicksBelow);
  this->glyph_quality_slider_->setTickInterval(1);
  this->glyph_quality_slider_->setMinimumWidth(200);
  this->ui_->glyphs_visible_button->setMenu(menu);

  layout->addWidget(this->glyph_size_slider_, 0, 2, 1, 1);
  layout->addWidget(this->glyph_auto_size_, 0, 3, 1, 1);
  layout->addWidget(this->glyph_quality_slider_, 1, 2, 1, 1);
  widget->setLayout(layout);

  QWidgetAction* widget_action = new QWidgetAction(widget);
  widget_action->setDefaultWidget(widget);
  menu->addAction(widget_action);

  //analysis tool initializations
  this->analysis_tool_ = QSharedPointer<AnalysisTool>(new AnalysisTool(preferences_));
  this->analysis_tool_->set_app(this);
  this->ui_->stacked_widget->addWidget(this->analysis_tool_.data());
  connect(this->analysis_tool_.data(), SIGNAL(update_view()), this,
          SLOT(handle_display_setting_changed()));
  connect(this->analysis_tool_.data(), SIGNAL(pca_update()), this, SLOT(handle_pca_update()));
  connect(this->analysis_tool_.data(), &AnalysisTool::progress,
          this, &ShapeWorksStudioApp::handle_progress);
  connect(this->analysis_tool_.data(), SIGNAL(reconstruction_complete()),
          this, SLOT(handle_reconstruction_complete()));
  connect(this->analysis_tool_.data(), &AnalysisTool::message,
          this, &ShapeWorksStudioApp::handle_message);
  connect(this->analysis_tool_.data(), &AnalysisTool::error,
          this, &ShapeWorksStudioApp::handle_error);


  // resize from preferences
  if (!this->preferences_.get_window_geometry().isEmpty()) {
    this->restoreGeometry(this->preferences_.get_window_geometry());
  }
  if (!this->preferences_.get_window_state().isEmpty()) {
    this->restoreState(this->preferences_.get_window_state());
  }

  // set to import
  this->ui_->action_import_mode->setChecked(true);
  this->ui_->stacked_widget->setCurrentIndex(VIEW_MODE::ORIGINAL);

  this->action_group_ = new QActionGroup(this);
  this->action_group_->addAction(this->ui_->action_import_mode);
  this->action_group_->addAction(this->ui_->action_groom_mode);
  this->action_group_->addAction(this->ui_->action_optimize_mode);
  this->action_group_->addAction(this->ui_->action_analysis_mode);

  this->ui_->statusbar->showMessage("ShapeWorks Studio");
  this->lightbox_ = LightboxHandle(new Lightbox());

  // visualizer initializations
  this->visualizer_ = QSharedPointer<Visualizer>(new Visualizer(preferences_));
  this->visualizer_->set_lightbox(this->lightbox_);

  // groom tool initializations
  this->groom_tool_ = QSharedPointer<GroomTool>(new GroomTool());
  this->ui_->stacked_widget->addWidget(this->groom_tool_.data());
  connect(this->groom_tool_.data(), SIGNAL(groom_complete()),
          this, SLOT(handle_groom_complete()));
  connect(this->groom_tool_.data(), SIGNAL(error_message(std::string)),
          this, SLOT(handle_error(std::string)));
  connect(this->groom_tool_.data(), &GroomTool::message,
          this, &ShapeWorksStudioApp::handle_message);
  connect(this->groom_tool_.data(), &GroomTool::progress,
          this, &ShapeWorksStudioApp::handle_progress);

  // optimize tool initializations
  this->optimize_tool_ = QSharedPointer<OptimizeTool>(new OptimizeTool());
  this->ui_->stacked_widget->addWidget(this->optimize_tool_.data());
  connect(this->optimize_tool_.data(), SIGNAL(optimize_complete()),
          this, SLOT(handle_optimize_complete()));

  connect(this->optimize_tool_.data(), &OptimizeTool::optimize_start, this,
          &ShapeWorksStudioApp::handle_optimize_start);

  connect(this->optimize_tool_.data(), SIGNAL(error_message(std::string)),
          this, SLOT(handle_error(std::string)));
  connect(this->optimize_tool_.data(), SIGNAL(warning_message(std::string)),
          this, SLOT(handle_warning(std::string)));
  connect(this->optimize_tool_.data(), &OptimizeTool::message,
          this, &ShapeWorksStudioApp::handle_message);
  connect(this->optimize_tool_.data(), &OptimizeTool::status,
          this, &ShapeWorksStudioApp::handle_status);
  connect(this->optimize_tool_.data(), &OptimizeTool::progress,
          this, &ShapeWorksStudioApp::handle_progress);

  // set up preferences window
  this->preferences_window_ =
    QSharedPointer<PreferencesWindow>(new PreferencesWindow(this, preferences_));
  this->preferences_window_->set_values_from_preferences();

  connect(this->preferences_window_.data(), SIGNAL(clear_cache()), this,
          SLOT(handle_clear_cache()));
  connect(this->preferences_window_.data(), SIGNAL(update_view()), this,
          SLOT(handle_color_scheme()));
  connect(this->preferences_window_.data(), SIGNAL(slider_update()), this,
          SLOT(handle_slider_update()));

  //regression tool TODO
  /*this->analysis_tool_ = QSharedPointer<AnalysisTool> (new AnalysisTool());
     this->analysis_tool_->set_project( this->session_ );
     this->analysis_tool_->set_app( this );
     this->ui_->lower_stacked_widget->addWidget( this->analysis_tool_.data() );*/

  this->new_session();

  this->update_from_preferences();
  this->update_display();

  // setup modes
  this->ui_->view_mode_combobox->addItem(Visualizer::MODE_ORIGINAL_C.c_str());
  this->ui_->view_mode_combobox->addItem(Visualizer::MODE_GROOMED_C.c_str());
  this->ui_->view_mode_combobox->addItem(Visualizer::MODE_RECONSTRUCTION_C.c_str());
  this->ui_->view_mode_combobox->setCurrentIndex(VIEW_MODE::ORIGINAL);
  this->set_view_combo_item_enabled(VIEW_MODE::ORIGINAL, true);
  this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, false);
  this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED, false);

  connect(this->ui_->features, qOverload<const QString&>(&QComboBox::currentIndexChanged), this,
          &ShapeWorksStudioApp::update_feature_map_selection);

  connect(this->ui_->feature_uniform_scale, &QCheckBox::toggled, this,
          &ShapeWorksStudioApp::set_feature_uniform_scale);

  //glyph options signals/slots
  connect(this->ui_->glyphs_visible_button, SIGNAL(clicked()), this, SLOT(handle_glyph_changed()));
  connect(this->ui_->surface_visible_button, SIGNAL(clicked()), this, SLOT(handle_glyph_changed()));
  connect(this->glyph_size_slider_, SIGNAL(valueChanged(int)), this, SLOT(handle_glyph_changed()));
  connect(this->glyph_quality_slider_, SIGNAL(valueChanged(int)), this,
          SLOT(handle_glyph_changed()));
  connect(this->glyph_auto_size_, &QCheckBox::clicked,
          this, &ShapeWorksStudioApp::handle_glyph_changed);
  this->preferences_.set_saved();
  this->enable_possible_actions();

  connect(this->ui_->actionAbout, &QAction::triggered, this, &ShapeWorksStudioApp::about);
  connect(this->ui_->actionKeyboard_Shortcuts, &QAction::triggered, this,
          &ShapeWorksStudioApp::keyboard_shortcuts);
}

//---------------------------------------------------------------------------
ShapeWorksStudioApp::~ShapeWorksStudioApp()
{}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::initialize_vtk()
{
  this->lightbox_->set_render_window(this->ui_->qvtkWidget->GetRenderWindow());
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_new_project_triggered()
{
  if (this->preferences_.not_saved() && this->ui_->action_save_project->isEnabled()) {
    // save the size of the window to preferences
    QMessageBox msgBox;
    msgBox.setText("Do you want to save your changes as a project file?");
    msgBox.setInformativeText("This will reload generated files and changed settings.");
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    int ret = msgBox.exec();
    if (ret == QMessageBox::Save) {
      if (!this->on_action_save_project_triggered()) {
        return;
      }
    }
    else if (ret == QMessageBox::Cancel) {
      return;
    }
  }

  this->new_session();

  this->update_table();
  this->update_from_preferences();

  this->lightbox_->clear_renderers();
  this->analysis_tool_->reset_stats();
  this->ui_->action_import_mode->setChecked(true);
  this->ui_->action_groom_mode->setChecked(false);
  this->ui_->action_optimize_mode->setChecked(false);
  this->ui_->action_analysis_mode->setChecked(false);
  this->ui_->stacked_widget->setCurrentWidget(this->ui_->import_page);
  this->ui_->controlsDock->setWindowTitle("Data");
  this->preferences_.set_saved();
  this->enable_possible_actions();
  this->update_display(true);
  this->visualizer_->update_viewer_properties();

  this->ui_->view_mode_combobox->setCurrentIndex(VIEW_MODE::ORIGINAL);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_actionShow_Tool_Window_triggered()
{
  this->ui_->controlsDock->setVisible(true);
  this->ui_->controlsDock->show();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_open_project_triggered()
{
  QString filename = QFileDialog::getOpenFileName(this, tr("Open Project..."),
                                                  this->preferences_.get_last_directory(),
                                                  tr("XLSX files (*.xlsx)"));
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());
  this->open_project(filename);
  this->enable_possible_actions();
}

//---------------------------------------------------------------------------
bool ShapeWorksStudioApp::on_action_save_project_triggered()
{
  if (this->session_->get_filename() == "") {
    return this->on_action_save_project_as_triggered();
  }
  else {
    this->save_project(this->session_->get_filename().toStdString());
  }
  return true;
}

//---------------------------------------------------------------------------
bool ShapeWorksStudioApp::on_action_save_project_as_triggered()
{
  QString last_directory = this->preferences_.get_last_directory();
  QString filename = QFileDialog::getSaveFileName(this, tr("Save Project As..."),
                                                  last_directory,
                                                  tr("XLSX files (*.xlsx)"));
  if (filename.isEmpty()) {
    return false;
  }

  if (!StringUtils::hasSuffix(filename.toStdString(), ".xlsx")) {
    filename = filename + ".xlsx";
  }

  this->preferences_.add_recent_file(filename);
  this->update_recent_files();

  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());

  this->save_project(filename.toStdString());

  this->enable_possible_actions();
  return false;
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_quit_triggered()
{
  this->close();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_import_triggered()
{
  auto filenames = QFileDialog::getOpenFileNames(this, tr("Import Files..."),
                                                 this->preferences_.get_last_directory(),
                                                 tr(
                                                   "Supported types (*.nrrd *.nii *.nii.gz *.mha *.vtk *.ply *.vtp *.obj *stl)"));

  if (filenames.size() == 0) {
    // was cancelled
    return;
  }

  this->preferences_.set_last_directory(QFileInfo(filenames[0]).absolutePath());

  //need to re-run everything if something new is added.
  this->ui_->view_mode_combobox->setCurrentIndex(VIEW_MODE::ORIGINAL);
  this->set_view_combo_item_enabled(VIEW_MODE::ORIGINAL, true);
  this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, false);
  this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED, false);

  this->visualizer_->set_display_mode(this->ui_->view_mode_combobox->currentText().toStdString());
  this->import_files(filenames);

  this->visualizer_->update_lut();
  this->enable_possible_actions();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::import_files(QStringList file_names)
{
  this->handle_message("Loading Files...");
  this->handle_progress(-1);
  QCoreApplication::processEvents();

  std::vector<std::string> list;
  for (auto& a : file_names) {
    list.push_back(a.toStdString());
  }
  try {

    bool first_load = false;

    if (this->session_->get_num_shapes() == 0 && file_names.size() > 0) {
      // if nothing is loaded, take the path of the first one as the relative path
      first_load = true;
      this->session_->set_project_path(QFileInfo(file_names[0]).absolutePath());
    }

    this->session_->load_original_files(list);

    this->session_->get_project()->store_subjects();

    this->update_table();
    this->enable_possible_actions();
    this->update_display(true);

    this->reset_num_viewers();

    if (first_load) {
      // On first load, we can check if there was an active scalar on loaded meshes
      this->set_feature_map(this->session_->get_default_feature_map());
    }

  } catch (std::runtime_error e) {
    this->handle_error(e.what());
  }
  this->handle_message("Files loaded");
  this->handle_progress(100);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_zoom_slider_valueChanged()
{
  if (!this->lightbox_->render_window_ready()) { return; }

  int value = this->ui_->zoom_slider->value();
  if (value == 0) {
    this->lightbox_->set_tile_layout(1, 1);
  }
  else if (value == 1) {
    this->lightbox_->set_tile_layout(2, 1);
  }
  else {
    this->lightbox_->set_tile_layout(value, value);
  }

  this->visualizer_->update_viewer_properties();

  this->update_scrollbar();

  this->ui_->qvtkWidget->GetRenderWindow()->Render();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::disable_all_actions()
{
  // export / save / new / open
  this->ui_->action_save_project->setEnabled(false);
  this->ui_->action_save_project_as->setEnabled(false);
  this->ui_->actionExport_PCA_Mesh->setEnabled(false);
  this->ui_->actionExport_Eigenvalues->setEnabled(false);
  this->ui_->actionExport_Eigenvectors->setEnabled(false);
  this->ui_->actionExport_PCA_Mode_Points->setEnabled(false);
  this->ui_->action_new_project->setEnabled(false);
  this->ui_->action_open_project->setEnabled(false);
  this->ui_->action_import->setEnabled(false);
  this->ui_->add_button->setEnabled(false);
  this->ui_->delete_button->setEnabled(false);
  this->ui_->menuExport->setEnabled(false);

  //subtools
  this->groom_tool_->disable_actions();
  this->optimize_tool_->disable_actions();
  //recent
  QStringList recent_files = preferences_.get_recent_files();
  int num_recent_files = qMin(recent_files.size(), 4);
  for (int i = 0; i < num_recent_files; i++) {
    this->recent_file_actions_[i]->setEnabled(false);
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::enable_possible_actions()
{
  // export / save / new / open
  bool reconstructed = this->session_->particles_present();

  bool original_present = this->session_->get_project()->get_segmentations_present();

  auto filename = this->session_->get_filename();
  bool save_enabled = filename == "" || filename.endsWith(".xlsx");
  this->ui_->action_save_project->setEnabled(save_enabled);
  //this->ui_->action_save_project_as->setEnabled(original_present);
  this->ui_->action_save_project_as->setEnabled(true);
  this->ui_->actionExport_PCA_Mesh->setEnabled(reconstructed);
  this->ui_->actionExport_Eigenvalues->setEnabled(reconstructed);
  this->ui_->actionExport_Eigenvectors->setEnabled(reconstructed);
  this->ui_->actionExport_PCA_Mode_Points->setEnabled(reconstructed);
  this->ui_->action_new_project->setEnabled(true);
  this->ui_->action_open_project->setEnabled(true);
  this->ui_->action_import->setEnabled(true);
  this->ui_->add_button->setEnabled(true);
  this->ui_->delete_button->setEnabled(true);
  this->ui_->menuExport->setEnabled(true);

  //available modes
  this->ui_->action_import_mode->setEnabled(true);
  this->ui_->action_groom_mode->setEnabled(original_present);
  this->ui_->action_optimize_mode->setEnabled(original_present);
  bool new_analysis = false;
  if (!this->ui_->action_analysis_mode->isEnabled() && reconstructed) {
    new_analysis = true;
  }
  this->ui_->action_analysis_mode->setEnabled(reconstructed);
  //subtools
  this->groom_tool_->enable_actions();
  this->optimize_tool_->enable_actions();
  this->analysis_tool_->enable_actions(new_analysis);
  //recent
  QStringList recent_files = preferences_.get_recent_files();
  int num_recent_files = qMin(recent_files.size(), 4);
  for (int i = 0; i < num_recent_files; i++) {
    this->recent_file_actions_[i]->setEnabled(true);
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::update_from_preferences()
{
  this->glyph_quality_slider_->setValue(preferences_.get_glyph_quality());
  this->glyph_size_slider_->setValue(preferences_.get_glyph_size() * 10.0);
  this->glyph_auto_size_->setChecked(preferences_.get_glyph_auto_size());
  this->glyph_size_slider_->setEnabled(!this->glyph_auto_size_->isChecked());

  this->glyph_quality_label_->setText(QString::number(preferences_.get_glyph_quality()));
  this->glyph_size_label_->setText(QString::number(preferences_.get_glyph_size()));

  this->ui_->center_checkbox->setChecked(preferences_.get_center_checked());
  this->groom_tool_->load_params();
  this->optimize_tool_->load_params();
  this->analysis_tool_->load_settings();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::update_scrollbar()
{
  int num_rows = this->lightbox_->get_num_rows();
  int num_visible = this->lightbox_->get_num_rows_visible();
  if (num_visible >= num_rows) {
    this->ui_->vertical_scroll_bar->setMaximum(0);
    this->ui_->vertical_scroll_bar->setEnabled(false);
  }
  else {
    this->ui_->vertical_scroll_bar->setEnabled(true);
    this->ui_->vertical_scroll_bar->setMaximum(num_rows - num_visible);
    this->ui_->vertical_scroll_bar->setPageStep(num_visible);
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_vertical_scroll_bar_valueChanged()
{
  int value = this->ui_->vertical_scroll_bar->value();
  this->lightbox_->set_start_row(value);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_add_button_clicked()
{
  this->on_action_import_triggered();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_delete_button_clicked()
{
  QModelIndexList list = this->ui_->table->selectionModel()->selectedRows();

  QList<int> index_list;
  for (int i = list.size() - 1; i >= 0; i--) {
    index_list << list[i].row();
  }

  this->session_->remove_shapes(index_list);
  if (this->session_->get_shapes().size() == 0) {
    this->new_session();
    this->analysis_tool_->reset_stats();
    this->lightbox_->clear_renderers();
  }
  this->update_table();
  this->update_display(true);
  this->enable_possible_actions();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::update_table()
{
  QVector<QSharedPointer<Shape>> shapes = this->session_->get_shapes();

  auto project = this->session_->get_project();
  auto headers = project->get_headers();

  QStringList table_headers;
  for (const std::string& header : headers) {
    //std::cerr << "header: " << header << "\n";
    table_headers << QString::fromStdString(header);
  }

  this->ui_->table->clear();
  this->ui_->table->setRowCount(shapes.size());
  this->ui_->table->setColumnCount(table_headers.size());

  this->ui_->table->setHorizontalHeaderLabels(table_headers);
  this->ui_->table->verticalHeader()->setVisible(true);

  for (int h = 0; h < table_headers.size(); h++) {
    auto rows = project->get_string_column(table_headers[h].toStdString());
    for (int row = 0; row < shapes.size() && row < rows.size(); row++) {
      QTableWidgetItem* new_item = new QTableWidgetItem(QString::fromStdString(rows[row]));
      this->ui_->table->setItem(row, h, new_item);
    }
  }

  this->ui_->table->resizeColumnsToContents();
  this->ui_->table->horizontalHeader()->setStretchLastSection(false);
  this->ui_->table->setSelectionBehavior(QAbstractItemView::SelectRows);


  /// todo: check if the list has changed before changing
  auto current_feature = this->ui_->features->currentText();
  this->ui_->features->clear();
  this->ui_->features->addItem("-none-");
  auto feature_maps = project->get_feature_names();
  for (const std::string& feature : feature_maps) {
    QString item = QString::fromStdString(feature);
    item = item.remove(0, 8);
    this->ui_->features->addItem(item);
  }
  this->ui_->features->setCurrentText(current_feature);
  this->ui_->feature_uniform_scale->setChecked(this->get_feature_uniform_scale());

  this->ui_->feature_widget->setVisible(feature_maps.size() > 0);

}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_pca_changed()
{
  if (!this->session_->particles_present()) { return; }
  this->session_->handle_clear_cache();
  this->visualizer_->update_lut();
  this->compute_mode_shape();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_slider_update()
{
  this->analysis_tool_->updateSlider();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_pca_update()
{
  if (this->analysis_tool_->get_active() &&
      this->analysis_tool_->get_analysis_mode() == AnalysisTool::MODE_PCA_C) {
    this->compute_mode_shape();
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_message(std::string str)
{
  if (str != this->current_message_) {
    STUDIO_LOG_MESSAGE(QString::fromStdString(str));
  }
  this->ui_->statusbar->showMessage(QString::fromStdString(str));
  this->current_message_ = str;
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_status(std::string str)
{
  this->ui_->statusbar->showMessage(QString::fromStdString(str));
  this->current_message_ = str;
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_error(std::string str)
{
  STUDIO_LOG_ERROR(QString::fromStdString(str));
  QMessageBox::critical(this, "Critical Error", str.c_str());
  this->handle_message(str);
  //this->handle_progress(100);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_warning(std::string str)
{
  STUDIO_LOG_MESSAGE(QString::fromStdString(str));
  QMessageBox::warning(this, "Warning!", str.c_str());
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_progress(int value)
{
  if (value < 0) {
    this->progress_bar_->setVisible(true);
    this->progress_bar_->setMinimum(0);
    this->progress_bar_->setMaximum(0);
    return;
  }
  this->progress_bar_->setMinimum(0);
  this->progress_bar_->setMaximum(100);

  if (value < 100) {
    this->progress_bar_->setVisible(true);
    this->progress_bar_->setValue(value);
    this->disable_all_actions();
  }
  else {
    this->progress_bar_->setValue(100);
    this->progress_bar_->setVisible(false);
    //this->enable_possible_actions();
  }
  this->handle_message(this->current_message_);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_new_mesh()
{
  this->visualizer_->handle_new_mesh();

  std::string mode = AnalysisTool::MODE_ALL_SAMPLES_C;
  if (this->ui_->action_analysis_mode->isChecked()) {
    mode = this->analysis_tool_->get_analysis_mode();
  }

  if (this->visualizer_->get_feature_map() != "" &&
      mode == AnalysisTool::MODE_MEAN_C) {
    this->visualizer_->display_shape(this->analysis_tool_->get_mean_shape());
  }

}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_clear_cache()
{
  this->handle_pca_changed();
  if (this->session_) {
    this->session_->handle_clear_cache();
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::new_session()
{

  // project initializations
  this->session_ = QSharedPointer<Session>(new Session(this, preferences_));
  this->session_->set_parent(this);
  this->setWindowTitle(this->session_->get_display_name());

  connect(this->session_->get_mesh_manager().data(), &MeshManager::error_encountered,
          this, &ShapeWorksStudioApp::handle_error);

  connect(this->session_.data(), SIGNAL(data_changed()), this, SLOT(handle_project_changed()));
  connect(this->session_.data(), SIGNAL(points_changed()), this, SLOT(handle_points_changed()));
  connect(this->session_.data(), SIGNAL(update_display()), this,
          SLOT(handle_display_setting_changed()));
  connect(this->session_.data(), SIGNAL(message(std::string)), this,
          SLOT(handle_message(std::string)));
  connect(this->session_.data(), SIGNAL(update_display()), this,
          SLOT(handle_display_setting_changed()));
  connect(this->session_.data(), &Session::new_mesh, this, &ShapeWorksStudioApp::handle_new_mesh);

  this->ui_->notes->setText("");

  this->visualizer_->clear_viewers();

  this->analysis_tool_->set_session(this->session_);
  this->visualizer_->set_session(this->session_);
  this->groom_tool_->set_session(this->session_);
  this->optimize_tool_->set_session(this->session_);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::update_tool_mode()
{
  std::string tool_state =
    this->session_->parameters().get("tool_state", Session::DATA_C);

  this->analysis_tool_->set_active(tool_state == Session::ANALYSIS_C);

  if (tool_state == Session::ANALYSIS_C) {
    this->ui_->stacked_widget->setCurrentWidget(this->analysis_tool_.data());
    this->ui_->controlsDock->setWindowTitle("Analysis");
    this->set_view_mode(Visualizer::MODE_RECONSTRUCTION_C);
    this->on_actionShow_Tool_Window_triggered();
    this->update_display();
    this->ui_->action_analysis_mode->setChecked(true);
  }
  else if (tool_state == Session::GROOM_C) {
    this->ui_->stacked_widget->setCurrentWidget(this->groom_tool_.data());
    this->groom_tool_->activate();
    this->ui_->controlsDock->setWindowTitle("Groom");
    this->set_view_mode(Visualizer::MODE_ORIGINAL_C);
    this->ui_->action_groom_mode->setChecked(true);
  }
  else if (tool_state == Session::OPTIMIZE_C) {
    this->ui_->stacked_widget->setCurrentWidget(this->optimize_tool_.data());
    this->optimize_tool_->activate();
    this->ui_->controlsDock->setWindowTitle("Optimize");
    if (this->session_->groomed_present()) {
      this->set_view_mode(Visualizer::MODE_GROOMED_C);
    }
    this->update_display();
    this->ui_->action_optimize_mode->setChecked(true);
  }
  else { // DATA
    this->ui_->stacked_widget->setCurrentIndex(VIEW_MODE::ORIGINAL);
    this->ui_->controlsDock->setWindowTitle("Data");
    this->ui_->action_import_mode->setChecked(true);
  }

  this->on_actionShow_Tool_Window_triggered();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::update_view_mode()
{
  auto view_mode = this->get_view_mode();
  this->ui_->view_mode_combobox->setCurrentText(QString::fromStdString(view_mode));

  auto feature_map = this->get_feature_map();
  this->ui_->features->setCurrentText(QString::fromStdString(feature_map));

  if (this->visualizer_) {
    //std::cerr << "Setting view mode to: " << view_mode << "\n";
    this->visualizer_->set_display_mode(view_mode);
    if (feature_map == "-none-") { feature_map = ""; }
    this->analysis_tool_->set_feature_map(feature_map);
    this->visualizer_->set_feature_map(feature_map);
    this->visualizer_->set_uniform_feature_range(this->get_feature_uniform_scale());
    this->update_display(true);
  }
}

//---------------------------------------------------------------------------
std::string ShapeWorksStudioApp::get_view_mode()
{
  return this->session_->parameters().get("view_state", Visualizer::MODE_ORIGINAL_C);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::set_view_combo_item_enabled(int item, bool value)
{
  this->ui_->view_mode_combobox->setItemData(item, value ? ITEM_ENABLE : ITEM_DISABLE, ITEM_ROLE);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_import_mode_triggered()
{
  this->session_->parameters().set("tool_state", Session::DATA_C);
  this->update_tool_mode();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_groom_mode_triggered()
{
  this->session_->parameters().set("tool_state", Session::GROOM_C);
  this->update_tool_mode();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_optimize_mode_triggered()
{
  this->session_->parameters().set("tool_state", Session::OPTIMIZE_C);
  this->update_tool_mode();
  this->visualizer_->reset_camera();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_analysis_mode_triggered()
{
  this->session_->parameters().set("tool_state", Session::ANALYSIS_C);
  this->update_tool_mode();
  this->visualizer_->reset_camera();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_project_changed()
{
  this->set_view_combo_item_enabled(VIEW_MODE::ORIGINAL, this->session_->original_present());
  this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, this->session_->groomed_present());
  this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED,
                                    this->session_->particles_present());

  if (this->session_->particles_present()) {
    this->session_->handle_clear_cache();
  }
  this->update_table();
  this->update_scrollbar();
  this->update_display();
  this->visualizer_->update_lut();

  this->enable_possible_actions();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_points_changed()
{
  bool update = false;
  if (!this->time_since_last_update_.isValid()) {
    update = true;
  }
  else {
    auto time_since = this->time_since_last_update_.elapsed();
    if (time_since > 25 + (this->last_render_ * 2)) {
      update = true;
    }
  }

  if (update) {
    double old_size = this->session_->get_auto_glyph_size();
    if (fabs(old_size - this->session_->update_auto_glyph_size()) > 0.5) {
      this->handle_glyph_changed();
    }

    QElapsedTimer render_time;
    render_time.start();
    this->visualizer_->update_samples();
    this->last_render_ = render_time.elapsed();
    this->time_since_last_update_.start();
  }

}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_optimize_complete()
{
  this->session_->get_mesh_manager()->get_surface_reconstructor()->resetReconstruct();
  this->analysis_tool_->reset_stats();
  this->analysis_tool_->initialize_mesh_warper();
  this->session_->handle_clear_cache();
  this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED, true);
  this->ui_->view_mode_combobox->setCurrentIndex(VIEW_MODE::GROOMED);
  this->visualizer_->set_display_mode(this->ui_->view_mode_combobox->currentText().toStdString());
  this->visualizer_->set_mean(this->analysis_tool_->get_mean_shape_points());
  this->visualizer_->update_lut();
  this->update_display();

  this->visualizer_->update_samples();
  this->handle_glyph_changed();
  this->enable_possible_actions();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_reconstruction_complete()
{
  this->session_->handle_clear_cache();
  this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED, true);
  this->visualizer_->set_mean(this->analysis_tool_->get_mean_shape_points());
  this->visualizer_->update_lut();
  this->update_display(true);
  this->enable_possible_actions();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_groom_complete()
{
  this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, true);
  this->ui_->view_mode_combobox->setCurrentIndex(VIEW_MODE::GROOMED);
  this->session_->handle_clear_cache();
  this->update_display(true);
  this->visualizer_->reset_camera();
  this->enable_possible_actions();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_optimize_start()
{
  this->ui_->action_analysis_mode->setEnabled(false);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_display_setting_changed()
{
  if (this->analysis_tool_->pcaAnimate()) { return; }
  this->update_display(true);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_glyph_changed()
{
  this->visualizer_->set_show_surface(this->ui_->surface_visible_button->isChecked());
  this->visualizer_->set_show_glyphs(this->ui_->glyphs_visible_button->isChecked());
  this->preferences_.set_glyph_size(this->glyph_size_slider_->value() / 10.0);
  this->preferences_.set_glyph_quality(this->glyph_quality_slider_->value());
  this->preferences_.set_glyph_auto_size(this->glyph_auto_size_->isChecked());
  this->glyph_size_slider_->setEnabled(!this->glyph_auto_size_->isChecked());
  if (this->glyph_auto_size_->isChecked()) {
    auto glyph_size = this->session_->get_auto_glyph_size();
    if (glyph_size > 0) {
      this->glyph_size_slider_->setValue(glyph_size * 10.0);
    }
  }

  this->glyph_quality_label_->setText(QString::number(preferences_.get_glyph_quality()));
  this->glyph_size_label_->setText(QString::number(preferences_.get_glyph_size()));
  this->update_display(true);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_center_checkbox_stateChanged()
{
  this->preferences_.set_center_checked(this->ui_->center_checkbox->isChecked());
  this->update_display(true);
  this->visualizer_->reset_camera();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::update_display(bool force)
{
  //if (!this->visualizer_ || this->session_->get_num_shapes() <= 0) {
  if (!this->visualizer_) {
    return;
  }

  if (this->block_update_) {
    return;
  }

  this->block_update_ = true;

  this->visualizer_->set_center(this->ui_->center_checkbox->isChecked());

  bool reconstruct_ready =
    this->session_->get_mesh_manager()->get_surface_reconstructor()->hasDenseMean();

  if (!this->session_->groomed_present() && this->session_->particles_present()) {
    // legacy will be used
    //std::cerr << "reconstruct ready\n";
    reconstruct_ready = true;
  }
  else {
    //std::cerr << "reconstruct not ready\n";
  }

  if (this->session_->particles_present()) {
    reconstruct_ready = true;
  }

  std::string mode = AnalysisTool::MODE_ALL_SAMPLES_C;

  if (this->ui_->action_analysis_mode->isChecked()) {
    mode = this->analysis_tool_->get_analysis_mode();
  }

  bool change = false;
  if (this->current_display_mode_ != mode) {
    change = true;
  }

  if (this->current_display_mode_ == mode && !force) {
    this->block_update_ = false;
    return;
  }

  this->current_display_mode_ = mode;

  if (mode == AnalysisTool::MODE_ALL_SAMPLES_C) {

    this->set_view_combo_item_enabled(VIEW_MODE::ORIGINAL, this->session_->original_present());
    this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, this->session_->groomed_present());
    this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED,
                                      this->session_->particles_present());

    this->session_->calculate_reconstructed_samples();
    this->visualizer_->display_samples();
  }
  else {
    if (mode == AnalysisTool::MODE_MEAN_C) {
      this->set_view_combo_item_enabled(VIEW_MODE::ORIGINAL, false);
      this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, false);
      this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED, true);

      this->set_view_mode(Visualizer::MODE_RECONSTRUCTION_C);
      this->visualizer_->set_mean(this->analysis_tool_->get_mean_shape_points());

      this->visualizer_->display_shape(this->analysis_tool_->get_mean_shape());
    }
    else if (mode == AnalysisTool::MODE_PCA_C) {
      this->set_view_combo_item_enabled(VIEW_MODE::ORIGINAL, false);
      this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, false);
      this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED, true);
      this->set_view_mode(Visualizer::MODE_RECONSTRUCTION_C);
      this->compute_mode_shape();
      this->visualizer_->reset_camera();
    }
    else if (mode == AnalysisTool::MODE_SINGLE_SAMPLE_C) {

      this->set_view_combo_item_enabled(VIEW_MODE::ORIGINAL, this->session_->original_present());
      this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, this->session_->groomed_present());
      this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED,
                                        this->session_->particles_present() &&
                                        reconstruct_ready);
      this->visualizer_->display_sample(this->analysis_tool_->get_sample_number());
      this->visualizer_->reset_camera();
    }
    else { //?
      this->set_view_combo_item_enabled(VIEW_MODE::ORIGINAL, this->session_->original_present());
      this->set_view_combo_item_enabled(VIEW_MODE::GROOMED, this->session_->groomed_present());
      this->set_view_combo_item_enabled(VIEW_MODE::RECONSTRUCTED,
                                        this->session_->particles_present() &&
                                        reconstruct_ready);
    } //TODO regression?

  }

  if (change && !this->is_loading_) { // do not override if loading
    this->reset_num_viewers();
  }

  this->update_scrollbar();

  this->block_update_ = false;
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_view_mode_combobox_currentIndexChanged(QString disp_mode)
{
  this->set_view_mode(disp_mode.toStdString());
  this->visualizer_->reset_camera();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::open_project(QString filename)
{
  this->new_session();
  this->handle_message("Loading Project: " + filename.toStdString());
  this->handle_progress(-1);

  try {
    if (!this->session_->load_project(filename)) {
      this->enable_possible_actions();
      this->handle_message("Project failed to load");
      this->handle_progress(100);
      return;
    }
  } catch (std::runtime_error e) {
    this->handle_error(e.what());
    this->handle_message("Project failed to load");
    this->handle_progress(100);
  }

  auto project = this->session_->get_project();
  if (project->get_version() > project->get_supported_version()) {
    this->handle_warning(
      "Warning: The project you have opened was created in a newer version of ShapeWorks\n\n"
      "Some features may not work and some settings may be incorrect or missing");
  }

  this->is_loading_ = true;
  this->analysis_tool_->reset_stats();
  this->analysis_tool_->initialize_mesh_warper();

  this->block_update_ = true;

  this->groom_tool_->load_params();
  this->optimize_tool_->load_params();
  this->preferences_window_->set_values_from_preferences();
  this->update_from_preferences();

  this->preferences_.add_recent_file(filename);
  this->update_recent_files();

  this->update_tool_mode();


  // set the zoom state
  //this->ui_->thumbnail_size_slider->setValue(
  //  this->preferences_.get_preference("zoom_state", 1));

  this->visualizer_->update_lut();
  this->preferences_.set_saved();
  this->enable_possible_actions();
  this->visualizer_->reset_camera();

  this->update_table();

  this->update_view_mode();

  // load analysis state
  std::string analysis_mode = this->session_->parameters().get("analysis_mode", "mean");
  this->analysis_tool_->set_analysis_mode(analysis_mode);

  int zoom_value = this->session_->parameters().get(ShapeWorksStudioApp::SETTING_ZOOM_C, "4");

  this->ui_->zoom_slider->setValue(zoom_value);

  this->ui_->notes->setText(QString::fromStdString(
    this->session_->parameters().get("notes", "")));

  this->block_update_ = false;
  this->update_display(true);

  this->on_zoom_slider_valueChanged();

  this->is_loading_ = false;

  if (this->session_->is_light_project()) {
    this->reset_num_viewers();
  }

  this->session_->update_auto_glyph_size();
  this->handle_glyph_changed();

  this->setWindowTitle(this->session_->get_display_name());

  this->handle_message("Project loaded");
  this->handle_progress(100);

}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_preferences_triggered()
{
  this->preferences_window_->show();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_export_current_mesh_triggered()
{
  auto dir = preferences_.get_last_directory() + "/";
  QString filename = QFileDialog::getSaveFileName(this, tr("Export Current Mesh"),
                                                  dir + "mesh",
                                                  tr("VTK files (*.vtk)"));
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());

  auto poly_data = this->visualizer_->get_current_mesh();
  vtkPolyDataWriter* writer = vtkPolyDataWriter::New();
  writer->SetFileName(filename.toStdString().c_str());
  writer->SetInputData(poly_data);
  writer->WriteArrayMetaDataOff();
  writer->Write();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_export_mesh_scalars_triggered()
{
  auto dir = preferences_.get_last_directory().toStdString() + "/";
  QString filename = QFileDialog::getSaveFileName(this, tr("Export Mesh Scalars"),
                                                  QString::fromStdString(dir) + "scalars",
                                                  tr("CSV files (*.csv)"));
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());

  auto poly_data = this->visualizer_->get_current_mesh();

  std::ofstream output;
  output.open(filename.toStdString().c_str());
  output << "point,x,y,z";

  auto scalars = poly_data->GetPointData()->GetScalars();
  scalars->SetName("scalar_values");

  //poly_data->GetPointData()->AddArray(scalars);
  int num_arrays = poly_data->GetPointData()->GetNumberOfArrays();

  for (int i = 0; i < num_arrays; i++) {
    if (!poly_data->GetPointData()->GetArrayName(i)) {
      output << "," << "scalars";
    }
    else {
      output << "," << poly_data->GetPointData()->GetArrayName(i);
      std::cout << "array: " << poly_data->GetPointData()->GetArrayName(i) << "\n";
    }
  }

  output << "\n";

  // iterate over vertices
  vtkPoints* points = poly_data->GetPoints();
  int num_points = points->GetNumberOfPoints();

  for (int i = 0; i < num_points; i++) {
    output << i;
    output << "," << poly_data->GetPoint(i)[0];
    output << "," << poly_data->GetPoint(i)[1];
    output << "," << poly_data->GetPoint(i)[2];

    for (int j = 0; j < num_arrays; j++) {
      output << "," << poly_data->GetPointData()->GetArray(j)->GetTuple(i)[0];
      //std::cout << "array: " << poly_data->GetPointData()->GetArrayName(i) << "\n";
    }

    output << "\n";
  }

  output.close();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_action_export_pca_scores_triggered()
{
  auto dir = preferences_.get_last_directory().toStdString() + "/";
  QString filename = QFileDialog::getSaveFileName(this, tr("Export PCA Scores"),
                                                  QString::fromStdString(dir) + "scores",
                                                  tr("CSV files (*.csv)"));
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());

  auto stats = this->analysis_tool_->get_stats();
  stats.PrincipalComponentProjections();

  stats.WriteCSVFile2(filename.toStdString());
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::closeEvent(QCloseEvent* event)
{
  // close the preferences window in case it is open
  this->preferences_window_->close();
  if (this->preferences_.not_saved() && this->ui_->action_save_project->isEnabled()) {
    // save the size of the window to preferences
    QMessageBox msgBox;
    msgBox.setText("Do you want to save your changes as a project file?");
    msgBox.setInformativeText("This will reload generated files and changed settings.");
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);
    int ret = msgBox.exec();
    if (ret == QMessageBox::Save) {
      if (!this->on_action_save_project_triggered()) {
        event->ignore();
        return;
      }
    }
    else if (ret == QMessageBox::Cancel) {
      event->ignore();
      return;
    }
  }
  this->analysis_tool_->shutdown();
  this->preferences_.set_window_geometry(this->saveGeometry());
  this->preferences_.set_window_state(this->saveState());

  this->optimize_tool_->shutdown_threads();
  STUDIO_CLOSE_LOG();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::compute_mode_shape()
{
  int pca_mode = this->analysis_tool_->getPCAMode();
  double pca_value = this->analysis_tool_->get_pca_value();

  this->visualizer_->display_shape(this->analysis_tool_->get_shape_points(pca_mode, pca_value));
}

//---------------------------------------------------------------------------
bool ShapeWorksStudioApp::set_view_mode(std::string view_mode)
{
  if (view_mode != this->get_view_mode()) {
    if (!this->is_loading_) {
      this->session_->parameters().set("view_state", view_mode);
    }
    this->update_view_mode();
    return true;
  }
  return false;
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::update_recent_files()
{
  QStringList recent_files = this->preferences_.get_recent_files();

  QStringList existing_files;
  for (int i = 0; i < recent_files.size(); i++) {
    if (QFile::exists(recent_files[i])) {
      existing_files << recent_files[i];
    }
  }
  recent_files = existing_files;

  QStringList no_dupes;
  for (int i = 0; i < recent_files.size(); i++) {
    bool found_dupe = false;
    for (int j = i + 1; j < recent_files.size(); j++) {
      if (QFileInfo(recent_files[i]).canonicalFilePath() ==
          QFileInfo(recent_files[j]).canonicalFilePath()) {
        found_dupe = true;
      }
    }
    if (!found_dupe) {
      no_dupes << recent_files[i];
    }
  }
  recent_files = no_dupes;

  int num_recent_files = qMin(recent_files.size(), 4); // only 4 max in the file menu

  for (int i = 0; i < num_recent_files; i++) {
    QString text = tr("&%1 %2").arg(i + 1).arg(QFileInfo(recent_files[i]).fileName());
    this->recent_file_actions_[i]->setText(text);
    this->recent_file_actions_[i]->setData(recent_files[i]);
    this->recent_file_actions_[i]->setVisible(true);
  }

  for (int j = num_recent_files; j < 4; ++j) {
    this->recent_file_actions_[j]->setVisible(false);
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::save_project(std::string filename)
{
  this->session_->parameters().set(ShapeWorksStudioApp::SETTING_ZOOM_C,
                                   std::to_string(this->ui_->zoom_slider->value()));

  this->session_->parameters().set("notes", this->ui_->notes->toHtml().toStdString());
  this->session_->parameters().set("analysis_mode", this->analysis_tool_->get_analysis_mode());

  this->groom_tool_->store_params();
  this->optimize_tool_->store_params();
  this->analysis_tool_->store_settings();

  if (this->session_->save_project(filename)) {
    this->handle_message("Project Saved");
  }

  this->update_table();
  this->setWindowTitle(this->session_->get_display_name());

}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_open_recent()
{
  QAction* action = qobject_cast<QAction*>(sender());
  if (action) {
    this->open_project(action->data().toString());
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::handle_color_scheme()
{
  this->visualizer_->update_viewer_properties();
  this->update_display();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_auto_view_button_clicked()
{
  this->visualizer_->reset_camera();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_actionExport_PCA_Mesh_triggered()
{
  auto dir = preferences_.get_last_directory().toStdString();
  dir = dir.substr(0, dir.find_last_of("/") + 1);
  QString filename = QFileDialog::getSaveFileName(this, tr("Export PCA Mesh"),
                                                  QString::fromStdString(dir) + "newMesh",
                                                  tr("VTK files (*.vtk)"));
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());
  if (this->analysis_tool_->get_analysis_mode() == "all samples") {
    auto shapes = this->session_->get_shapes();
    for (size_t i = 0; i < shapes.size(); i++) {
      auto msh = shapes[i]->get_reconstructed_mesh()->get_poly_data();
      vtkPolyDataWriter* writer = vtkPolyDataWriter::New();
      auto name = filename.toStdString();
      name = name.substr(0, name.find_last_of(".")) + std::to_string(i) + ".vtk";
      writer->SetFileName(name.c_str());
      writer->SetInputData(msh);
      writer->WriteArrayMetaDataOff();
      writer->Write();
    }
    this->handle_message("Successfully exported PCA Mesh files: " + filename.toStdString());
    return;
  }
  auto shape = this->visualizer_->getCurrentShape();
  /// TODO: fix
  //auto msh = this->session_->get_mesh_manager()->get_mesh(shape);

  vtkPolyDataWriter* writer = vtkPolyDataWriter::New();
  writer->SetFileName(filename.toStdString().c_str());
  /// TODO: fix
  //writer->SetInputData(msh);
  writer->WriteArrayMetaDataOff();
  writer->Write();
  this->handle_message("Successfully exported PCA Mesh file: " + filename.toStdString());
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_actionExport_Eigenvalues_triggered()
{
  auto stats = this->analysis_tool_->get_stats();
  auto values = stats.Eigenvalues();
  QString fname("Untitled.eval");

  auto dir = this->preferences_.get_last_directory().toStdString() + "/";
  QString filename = QFileDialog::getSaveFileName(this, tr("Export Eigenvalue EVAL file..."),
                                                  QString::fromStdString(dir) + fname,
                                                  tr("EVAL files (*.eval)"));
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());
  std::ofstream out(filename.toStdString().c_str());
  for (size_t i = values.size() - 1; i > 0; i--) {
    out << values[i] << std::endl;
  }
  out.close();
  this->handle_message("Successfully exported eigenvalue EVAL file: " + filename.toStdString());
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_actionExport_Eigenvectors_triggered()
{
  auto stats = this->analysis_tool_->get_stats();
  auto values = stats.Eigenvectors();
  QString fname("Untitled.eval");
  auto dir = this->preferences_.get_last_directory().toStdString() + "/";
  QString filename = QFileDialog::getSaveFileName(this, tr("Export Eigenvector EVAL files..."),
                                                  QString::fromStdString(dir) + fname,
                                                  tr("EVAL files (*.eval)"));
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());
  auto basename =
    filename.toStdString().substr(0, filename.toStdString().find_last_of(".eval") - 4);
  for (size_t i = values.columns() - 1, ii = 0; i > 0; i--, ii++) {
    auto col = values.get_column(i);
    std::ofstream out(basename + std::to_string(ii) + ".eval");
    size_t newline = 1;
    for (auto& a : col) {
      out << a << (newline % 3 == 0 ? "\n" : "    ");
      newline++;
    }
    out.close();
  }
  this->handle_message("Successfully exported eigenvalue EVAL file: " + filename.toStdString());
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_actionExport_PCA_Mode_Points_triggered()
{
  QString fname("Untitled.pts");
  auto dir = this->preferences_.get_last_directory().toStdString() + "/";
  QString filename = QFileDialog::getSaveFileName(this, tr("Save PCA Mode PCA files..."),
                                                  QString::fromStdString(dir) + fname,
                                                  tr("PTS files (*.pts)"));
  auto basename = filename.toStdString().substr(0,
                                                filename.toStdString().find_last_of(".pts") - 3);
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());

  float range = this->preferences_.get_pca_range();
  float steps = static_cast<float>(this->preferences_.get_pca_steps());

  int mode = this->analysis_tool_->getPCAMode();
  auto increment = range * 2.f / steps;
  size_t i = 0;
  for (float pca = -range; pca <= range; pca += increment, i++) {
    auto pts = this->analysis_tool_->get_shape_points(mode, pca);
    std::ofstream out(basename + std::to_string(mode) + "-" + std::to_string(i) + ".pts");
    size_t newline = 1;
    for (auto& a : pts) {
      out << a << (newline % 3 == 0 ? "\n" : "    ");
      newline++;
    }
    out.close();
  }
  this->handle_message("Successfully exported PCA Mode PTS files: " + filename.toStdString());
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::on_actionExport_Variance_Graph_triggered()
{
  QString fname("Untitled.png");
  auto dir = this->preferences_.get_last_directory().toStdString();
  dir = dir.substr(0, dir.find_last_of("/") + 1);
  QString filename = QFileDialog::getSaveFileName(this, tr("Export Variance Graph"),
                                                  QString::fromStdString(dir) + fname,
                                                  tr("PNG files (*.png)"));
  if (filename.isEmpty()) {
    return;
  }
  this->preferences_.set_last_directory(QFileInfo(filename).absolutePath());

  if (!this->analysis_tool_->export_variance_graph(filename)) {
    this->handle_error("Error writing variance graph");
  }
  else {
    this->handle_message("Successfully exported Variance Graph: " + filename.toStdString());
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::update_feature_map_selection(const QString& feature_map)
{
  this->set_feature_map(feature_map.toStdString());
}

//---------------------------------------------------------------------------
bool ShapeWorksStudioApp::set_feature_map(std::string feature_map)
{
  if (feature_map != this->get_feature_map()) {
    if (!this->is_loading_) {
      this->session_->parameters().set("feature_map", feature_map);
    }
    this->update_view_mode();
    return true;
  }
  return false;
}

//---------------------------------------------------------------------------
std::string ShapeWorksStudioApp::get_feature_map()
{
  return this->session_->parameters().get("feature_map", "");
}

//---------------------------------------------------------------------------
bool ShapeWorksStudioApp::get_feature_uniform_scale()
{
  return this->session_->parameters().get("feature_uniform_scale", true);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::set_feature_uniform_scale(bool value)
{
  if (!this->is_loading_) {
    this->session_->parameters().set("feature_uniform_scale", value);
    this->update_view_mode();
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::show_splash_screen()
{
  this->splash_screen_->show();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::about()
{
  std::string about = std::string("About ShapeWorksStudio");
  QMessageBox::about(this, QString::fromStdString(about),
                     "ShapeWorksStudio\n"
                     SHAPEWORKS_VERSION
                     "\n\n"
                     "http://shapeworks.sci.utah.edu");
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::keyboard_shortcuts()
{
  KeyboardShortcuts dialog(this);
  dialog.exec();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::reset_num_viewers()
{
  std::string mode = AnalysisTool::MODE_ALL_SAMPLES_C;

  if (this->ui_->action_analysis_mode->isChecked()) {
    mode = this->analysis_tool_->get_analysis_mode();
  }

  if (mode == AnalysisTool::MODE_ALL_SAMPLES_C) {
    size_t num_samples = this->session_->get_shapes().size();
    int value = 4;
    if (num_samples == 1) {
      value = 0; // single
    }
    else if (num_samples == 2) {
      value = 1; // two side by side
    }
    else if (num_samples <= 4) {
      value = 2; // 2x2
    }
    else if (num_samples <= 9) {
      value = 3; // 3x3
    }
    else if (num_samples > 9) {
      value = 4; // 4x4
    }
    if (value != this->ui_->zoom_slider->value()) {
      this->ui_->zoom_slider->setValue(value);
    }
  }
  else {
    if (0 != this->ui_->zoom_slider->value()) {
      this->ui_->zoom_slider->setValue(0);
    }
  }
  this->on_zoom_slider_valueChanged();
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::dragEnterEvent(QDragEnterEvent* event)
{
  bool accept = false;

  if (event->mimeData()->hasUrls()) {
    QList<QUrl> urls = event->mimeData()->urls();

    for (int i = 0; i < urls.size(); ++i) {
      std::string filename = urls[i].toLocalFile().toStdString();
      if (Session::is_supported_file_format(filename)) {
        accept = true;
      }
    }
  }

  if (accept) {
    this->setFocus();
    event->accept();
  }
  else {
    event->ignore();
  }
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::dragLeaveEvent(QDragLeaveEvent* event)
{
  this->clearFocus();
  QWidget::dragLeaveEvent(event);
}

//---------------------------------------------------------------------------
void ShapeWorksStudioApp::dropEvent(QDropEvent* event)
{
  bool accept = false;

  QStringList files_to_load;

  if (event->mimeData()->hasUrls()) {
    QList<QUrl> urls = event->mimeData()->urls();

    for (int i = 0; i < urls.size(); ++i) {
      std::string filename = urls[i].toLocalFile().toStdString();

      if (Session::is_supported_file_format(filename)) {
        accept = true;
        files_to_load << QString::fromStdString(filename);
      }
    }
  }

  if (accept) {
    this->import_files(files_to_load);
    event->accept();
  }
  else {
    event->ignore();
  }
}


//---------------------------------------------------------------------------

}