#pragma once

#include <Eigen/Dense>
#include <Eigen/Sparse>

#include <QString>
#include <QSharedPointer>

#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkImageImport.h>
#include <itkImage.h>

#include <vnl/vnl_vector.h>

using PixelType = float;
using ImageType = itk::Image<PixelType, 3>;

namespace shapeworks {

class StudioMesh;
typedef QSharedPointer<StudioMesh> MeshHandle;
//! Representation of a single mesh.
/*!
 * The Mesh class represents a single mesh generated from an image file or set of particles.
 * It is responsible for loading the image and generating a mesh from it.
 *
 */
class StudioMesh {
public:

  //! Constructor
  StudioMesh();

  //! Destructor
  ~StudioMesh();

  //! Create a mesh from an image
  void create_from_image(ImageType::Pointer img, double iso_value);

  //! Get the dimensions as a string for display (if loaded from an image)
  QString get_dimension_string();

  //! Get the mesh polydata
  vtkSmartPointer<vtkPolyData> get_poly_data();

  //! Get the center transform
  vnl_vector<double> get_center_transform();

  //! Set the poly data directly
  void set_poly_data(vtkSmartPointer<vtkPolyData> poly_data);

  //! Set the error message
  void set_error_message(std::string error_message);

  //! Return the error message
  std::string get_error_message();

  //! Apply a feature map
  void apply_feature_map(std::string name, ImageType::Pointer image,
                         vnl_vector<double> transform);

  //! Apply scalars from another mesh, with a transform
  void apply_scalars(QSharedPointer<StudioMesh> mesh, vnl_vector<double> transform);

  //! Interpolation scalars at positions to this mesh
  void interpolate_scalars_to_mesh(std::string name,
                                   vnl_vector<double> positions, Eigen::VectorXf scalar_values);

private:

  // metadata
  int dimensions_[3];
  vnl_vector<double> center_transform_;

  // the polydata
  vtkSmartPointer<vtkPolyData> poly_data_;

  // error message if the polydata didn't load
  std::string error_message_;

};
}