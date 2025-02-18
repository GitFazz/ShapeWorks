#pragma once
/**
 * @file MeshManager.h
 * @brief Class to manage meshes
 *
 * The MeshManager handles all aspects of mesh generation and caching.
 * It houses the cache and manages threads to work on mesh generation
 * in the background.
 */

#include <vtkSmartPointer.h>

#include <QWaitCondition>
#include <QThreadPool>


#include <Data/MeshCache.h>
#include <Data/MeshGenerator.h>
#include <Data/MeshWorkQueue.h>
#include <Data/MeshWorker.h>

#include <Data/Preferences.h>
#include <Data/SurfaceReconstructor.h>
#include <Data/MeshWarper.h>


namespace shapeworks {

class MeshManager : public QObject {
Q_OBJECT

public:
  MeshManager(Preferences& prefs);
  ~MeshManager();

  //! generate and cache a mesh for this shape in a different thread
  void generate_mesh(const MeshWorkItem item);

  //! get a mesh for a MeshWorkItem
  MeshHandle get_mesh(const MeshWorkItem& item, bool wait = false);

  //! get a mesh for a set of points
  MeshHandle get_mesh(const vnl_vector<double>& points);

  //! return the surface reconstructor
  QSharedPointer<SurfaceReconstructor> get_surface_reconstructor();

  //! return the mesh warper
  QSharedPointer<MeshWarper> get_mesh_warper();

  //! return the mesh generator
  QSharedPointer<MeshGenerator> get_mesh_generator()
  { return this->mesh_generator_; }

  //! clear the cache
  void clear_cache();

public Q_SLOTS:

  void handle_thread_complete(const MeshWorkItem& item, MeshHandle mesh);

Q_SIGNALS:

  void new_mesh();

  void error_encountered(std::string message);

private:

  void check_error_status(MeshHandle mesh);

  Preferences& prefs_;

  // cache of shape meshes
  MeshCache mesh_cache_;

  // the mesh generator
  QSharedPointer<MeshGenerator> mesh_generator_ = QSharedPointer<MeshGenerator>::create();

  // queue of meshes to build
  MeshWorkQueue work_queue_;

  QSharedPointer<SurfaceReconstructor> surface_reconstructor_;

  QSharedPointer<MeshWarper> mesh_warper_ = QSharedPointer<MeshWarper>::create();

  QThreadPool thread_pool_;

  bool error_emitted_ = false;
};

}