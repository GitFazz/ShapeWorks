# -*- coding: utf-8 -*-
"""
=====================================================================
Fixed Domains Example for Statistical Shape Modeling using ShapeWorks 
=====================================================================

In this example we work with a dataset of axis aligned ellipsoids. 
This examples is a use case for fixed domains, i.e. we have an existing 
shape model using some ellipsoids and we want to place correspondences 
on new ellisoids (we are provided fully prepped binary images) according 
to the existing shape model.
"""
import os
import sys
import csv
import argparse

from GroomUtils import *
from OptimizeUtils import *
from AnalyzeUtils import *
import CommonUtils

def Run_Pipeline(args):

    """
    Unzip the data for this tutorial.

    The data is inside the EllipsoidsFD.zip, run the following function to unzip the 
    data and create necessary supporting files. The zip will download to /Data and
    the files will be extracted in a newly created directory Output/ellipsoids_fd. 
    This contains the existing shape model and all the necessary files plus the 
    new scans to be processed.
    """

    print("\nStep 1. Extract Data\n")
    if int(args.interactive) != 0:
        input("Press Enter to continue")

    datasetName = "ellipsoid_1mode"
    outputDirectory = "Output/ellipsoid_fd/"
    if not os.path.exists(outputDirectory):
        os.makedirs(outputDirectory)
    CommonUtils.download_and_unzip_dataset(datasetName, outputDirectory)

    fileListDT = sorted(glob.glob(outputDirectory + datasetName + "/groomed/distance_transforms/*.nrrd"))
    fileListNew = sorted(glob.glob(outputDirectory + datasetName + "/fd_segmentations/*.nrrd"))

    """
    ## GROOM : Data Pre-processing 
    These ellipsoids are prepped and the new ellipsoids just needs to be converted to
    distance transforms.
    """

    groomDir = outputDirectory + 'groomed/'
    if not os.path.exists(groomDir):
        os.makedirs(groomDir)

    print("\nStep 2. Groom - Convert to distance transforms\n")
    if int(args.interactive) != 0:
        input("Press Enter to continue")

    dtFilesNew = applyDistanceTransforms(groomDir, fileListNew)
    dtFiles = fileListDT + dtFilesNew

    """
    ## OPTIMIZE : Particle Based Optimization with Fixed Domains

    Now that we have the distance transform representation of data we create 
    the parameter files for the shapeworks particle optimization routine.
    For more details on the plethora of parameters for shapeworks please refer to
    docs/workflow/optimize.md

    First we need to create a dictionary for all the parameters required by this
    optimization routine
    """

    print("\nStep 4. Optimize - Particle Based Optimization\n")
    if int(args.interactive) != 0:
        input("Press Enter to continue")

    pointDir = outputDirectory + 'shape_models/'
    if not os.path.exists(pointDir):
        os.makedirs(pointDir)

    """
    Evaluate the meanshape of the existing shape model and use that to initialize the 
    particles on the new shapes
    """
    shapemodelDir =  outputDirectory + datasetName + "/shape_models/ellipsoid/128/"
    print(os.listdir(shapemodelDir))
    findMeanShape(shapemodelDir)
    meanShapePath = shapemodelDir + '/meanshape_local.particles'

    """
    Read the parameter file used for creating the existing shape model 
    and decipher the parameters
    """

    parameterDictionary = {
        "number_of_particles" : 128,
        "use_normals": 0,
        "normal_weight": 15.0,
        "checkpointing_interval" : 200,
        "keep_checkpoints" : 0,
        "iterations_per_split" : 100,
        "optimization_iterations" : 2000,
        "starting_regularization" : 100,
        "ending_regularization" : 0.1,
        "recompute_regularization_interval" : 2,
        "domains_per_shape" : 1,
        "domain_type" : 'image',
        "relative_weighting" : 15,
        "initial_relative_weighting" : 0.05,
        "procrustes_interval" : 0,
        "procrustes_scaling" : 0,
        "save_init_splits" : 0,
        "verbosity" : 2,
        "number_fixed_domains": len(fileListDT),
        "fixed_domain_model_dir": shapemodelDir,
        "mean_shape_path": meanShapePath,
    }

    [localPointFiles, worldPointFiles] = runShapeWorksOptimize_FixedDomains(pointDir, dtFiles, parameterDictionary)

    """
    ## ANALYZE : Shape Analysis and Visualization

    Shapeworks yields relatively sparse correspondence models that may be inadequate to reconstruct 
    thin structures and high curvature regions of the underlying anatomical surfaces. 
    However, for many applications, we require a denser correspondence model, for example, 
    to construct better surface meshes, make more detailed measurements, or conduct biomechanical 
    or other simulations on mesh surfaces. One option for denser modeling is 
    to increase the number of particles per shape sample. However, this approach necessarily 
    increases the computational overhead, especially when modeling large clinical cohorts.

    Here we adopt a template-deformation approach to establish an inter-sample dense surface correspondence, 
    given a sparse set of optimized particles. To avoid introducing bias due to the template choice, we developed
    an unbiased framework for template mesh construction. The dense template mesh is then constructed 
    by triangulating the isosurface of the mean distance transform. This unbiased strategy will preserve 
    the topology of the desired anatomy  by taking into account the shape population of interest. 
    In order to recover a sample-specific surface mesh, a warping function is constructed using the 
    sample-level particle system and the mean/template particle system as control points. 
    This warping function is then used to deform the template dense mesh to the sample space.
    """

    print("\nStep 5. Analysis - Launch ShapeWorksStudio - sparse correspondence model.\n")
    if args.interactive != 0:
        input("Press Enter to continue")

    launchShapeWorksStudio(pointDir, dtFiles, localPointFiles, worldPointFiles)
