# -*- coding: utf-8 -*-
"""
====================================================================
Full Example Pipeline for Statistical Shape Modeling with ShapeWorks
====================================================================
This example is similar to the ellipsoid use case but a cutting plane 
is used in optimization.
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
    The data (ellipsoid-aligned.zip) is dowloaded to /Data folder and extracted to 
    /Output/ellpsoid_cut
    """

    print("\nStep 1. Extract Data\n")
    if int(args.interactive) != 0:
        input("Press Enter to continue")
    # Get data
    datasetName = "ellipsoid_1mode_aligned"
    outputDirectory = "Output/ellipsoid_cut/"
    if not os.path.exists(outputDirectory):
        os.makedirs(outputDirectory)
    
    
    #If tiny_test then download subset of the data
    if args.tiny_test:
        args.use_single_scale = 1
        CommonUtils.download_subset(args.use_case,datasetName, outputDirectory)
        fileList = sorted(glob.glob(outputDirectory + datasetName + "/segmentations/*.nrrd"))[:3]
    #else download the entire dataset
    else:
        CommonUtils.download_and_unzip_dataset(datasetName, outputDirectory)
        fileList = sorted(glob.glob(outputDirectory + datasetName + "/segmentations/*.nrrd"))
    # Select data if using subsample
    if args.use_subsample:
        sample_idx = sampledata(fileList, int(args.num_subsample))
        fileList = [fileList[i] for i in sample_idx]
    else:
        sample_idx = []

    print("\nStep 2. Get distance transforms\n")
    groomDir = outputDirectory + 'groomed/'
    if not os.path.exists(groomDir):
        os.makedirs(groomDir)
    dtFiles = applyDistanceTransforms(groomDir, fileList)

    """
    ## OPTIMIZE : Particle Based Optimization

    Now that we have the distance transform representation of data we create
    the parameter files for the shapeworks particle optimization routine.
    For more details on the plethora of parameters for shapeworks please refer to
    /docs/workflow/optimize.md

    First we need to create a dictionary for all the parameters required by this
    optimization routine
    """

    print("\nStep 4. Optimize - Particle Based Optimization\n")
    if int(args.interactive) != 0:
        input("Press Enter to continue")

    pointDir = outputDirectory + 'shape_models/'
    if not os.path.exists(pointDir):
        os.makedirs(pointDir)

    cutting_plane_points1 = [[10, 10, 0], [-10, -10, 0], [10, -10, 0]]
    cutting_plane_points2 = [[10, -3, 10], [-10, -3 ,10], [10, -3, -10]]
    cp = [cutting_plane_points1, cutting_plane_points2]

    # Cutting planes
    cutting_planes = []
    cutting_plane_counts = []
    for i in range(len(dtFiles)):
        cutting_planes.append(cutting_plane_points1)
        cutting_planes.append(cutting_plane_points2)
        cutting_plane_counts.append(2)

    parameterDictionary = {
        "number_of_particles": 32,
        "use_normals": 1,
        "normal_weight": 15.0,
        "checkpointing_interval": 200,
        "keep_checkpoints": 0,
        "iterations_per_split": 3000,
        "optimization_iterations": 3000,
        "starting_regularization": 100,
        "ending_regularization": 10,
        "recompute_regularization_interval": 2,
        "domains_per_shape": 1,
        "domain_type": 'image',
        "relative_weighting": 15,
        "initial_relative_weighting": 0.05,
        "procrustes_interval": 0,
        "procrustes_scaling": 0,
        "save_init_splits": 0,
        "verbosity": 2,
        "adaptivity_mode": 0,
        "cutting_plane_counts": cutting_plane_counts,
        "cutting_planes": cutting_planes
    }

    if args.tiny_test:
        parameterDictionary["number_of_particles"] = 16
        parameterDictionary["optimization_iterations"] = 25

    if not args.use_single_scale:
        parameterDictionary["use_shape_statistics_after"] = 16

    """
    Now we execute a single scale particle optimization function.
    """
    [localPointFiles, worldPointFiles] = runShapeWorksOptimize(pointDir, dtFiles, parameterDictionary)

    if args.tiny_test:
        print("Done with tiny test")
        exit()

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
