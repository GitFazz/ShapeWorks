# -*- coding: utf-8 -*-
"""
Same as femur.py, but uses the c++ implementation of cutting planes.
"""
import os
import sys
import csv
import argparse
import glob
import re
import numpy as np
import pickle

from GroomUtils import *
from OptimizeUtils import *
from AnalyzeUtils import *
import CommonUtils

def Run_Pipeline(args):
    """
    Get the data for this tutorial.
    If femur.zip is not there it will be downloaded from the ShapeWorks data portal.
    femur.zip will be unzipped and the data will be extracted in a newly created Directory TestFemur_cut.
    This data is femur segmentation and the unsegmented hip CT scan.
    """

    print("\nStep 1. Get Data\n")
    if int(args.interactive) != 0:
        input("Press Enter to continue")
    # Get data
    datasetName = "femur-v0"
    outputDirectory = "Output/femur_cut/"
    if not os.path.exists(outputDirectory):
        os.makedirs(outputDirectory)

    if not args.groom_images:
        # set spacing
        spacing = [1, 1, 1]
        answer = input("Use isotropic spacing for mesh rasterization? y/n \n")
        if answer == 'n':
            done = False
            while not done:
                spacing = []
                spacing.append(float(input("Enter x spacing:\n")))
                spacing.append(float(input("Enter y spacing:\n")))
                spacing.append(float(input("Enter z spacing:\n")))
                answer2 = input('Is spacing = ' + str(spacing) + ' okay? y/n\n')
                if answer2 == 'y':
                    done = True

    #If tiny_test then download subset of the data
    if args.tiny_test:
        args.use_single_scale = True
        args.interactive = False
        CommonUtils.download_subset(args.use_case,datasetName, outputDirectory)
        files_mesh = sorted(glob.glob(outputDirectory + datasetName + "/meshes/*.ply"))[:3]
        files_img = sorted(glob.glob(outputDirectory + datasetName + "/images/*.nrrd"))[:3]
    #else download the entire dataset
    else:
        CommonUtils.download_and_unzip_dataset(datasetName, outputDirectory)
        files_mesh = sorted(glob.glob(outputDirectory + datasetName + "/meshes/*.ply"))
        files_img = sorted(glob.glob(outputDirectory + datasetName + "/images/*.nrrd"))

    # Select data if using subsample
    if args.use_subsample:
        sample_idx = sampledata(files_img, int(args.num_subsample))
        files_img = [files_img[i] for i in sample_idx]
        files_mesh = [files_mesh[i] for i in sample_idx]
    else:
        sample_idx = []

    """
    ## GROOM : Data Pre-processing
    For the unprepped data the first few steps are
    -- if no interactive tag - use pre-defined cutting plane
    -- if interacitve tag and option 1 is chosen - define cutting plane on sample of users choice
    -- Reflect images and meshes
    -- Turn meshes to volumes
    -- Isotropic resampling
    -- Padding
    -- Center of Mass Alignment
    -- Centering
    -- Rigid Alignment
    -- if interactive tag and option 2 was chosen - define cutting plane on mean sample
    -- clip segementations with cutting plane
    -- find largest bounding box and crop
    """
    if args.skip_grooming:
        print("Skipping grooming.")
        dtDirecory = outputDirectory + datasetName + '/groomed/distance_transforms/'
        indices = []
        if args.tiny_test:
            indices = [0,1,2]
        elif args.use_subsample:
            indices = sample_idx
        dtFiles = CommonUtils.get_file_list(dtDirecory, ending=".nrrd", indices=indices)
        [cutting_plane_points] = pickle.load( open( outputDirectory + "groomed/groomed_pickle.p", "rb" ) )
    else:
        print("\nStep 2. Groom - Data Pre-processing\n")
        if args.interactive:
            input("Press Enter to continue")

        # Directory where grooming output folders will be added
        parentDir = outputDirectory + 'groomed/'
        if not os.path.exists(parentDir):
            os.mkdir(parentDir)

        # set name specific variables
        img_suffix = "1x_hip"
        reference_side = "left" # somewhat arbitrary, could be right

        # If not interactive, set cutting plane
        if not args.interactive:
            cutting_plane_points = np.array([[-1.0, -1.0,-40.5],[1.0,-1.0,-40.5],[-1.0,1.0, -40.5]])
            cp_prefix = 'm03_L'
            choice = 0

        # If interactive ask whether to define on chosen sample or median
        else:
            choice_made = False
            while not choice_made:
                print("\nOption 1: Define cutting plane now on a sample of your choice.")
                print("Option 2: Define cutting plane on median sample once it has been selected.")
                choice = input("Please input 1 or 2 and press enter: ")
                choice = int(choice)
                if choice==1 or choice==2:
                    choice_made = True

        # If user chose option 1, define cutting plane on sample of their choice
        if choice == 1:
            options = []
            for file in files_mesh:
                file = file.split('/')[-1]
                prefix = "_".join(file.split("_")[:2])
                options.append(prefix)
            input_mesh = ''
            while not input_mesh:
                print("\n\nType the prefix of the sample you wish to use to select the cutting plane from listed options and press enter.\nOptions: " + ", ".join(options) + '\n')
                cp_prefix = input()
                if cp_prefix:
                    for file in files_mesh:
                        if cp_prefix in file:
                            input_mesh = file
                if not input_mesh:
                    print("Invalid prefix.")
            if cp_prefix[-1] == 'R':
                reference_side = "right"

        # BEGIN GROOMING WITH IMAGES
        if args.groom_images and files_img:
            """
            Reflect - We have left and right femurs, so we reflect both image and mesh
            for the non-reference side so that all of the femurs can be aligned.
            """
            reflectedFiles_mesh, reflectedFile_img = anatomyPairsToSingles(parentDir + 'reflected', files_mesh, files_img, reference_side)

            """
            MeshesToVolumes - Shapeworks requires volumes so we need to convert
            mesh segementaions to binary segmentations.
            """
            fileList_seg = MeshesToVolumesUsingImages(parentDir + "volumes", reflectedFiles_mesh, reflectedFile_img)

            """
            Apply isotropic resampling
            The segmentation and images are resampled independently to have uniform spacing.
            """
            isoresampledFiles_segmentations = applyIsotropicResampling(parentDir + "resampled/segmentations", fileList_seg, isBinary=True)
            isoresampledFiles_images = applyIsotropicResampling(parentDir + "resampled/images", reflectedFile_img, isBinary=False)

            """
            Apply padding
            Both the segmentation and raw images are padded in case the seg lies on the image boundary.
            """
            paddedFiles_segmentations = applyPadding(parentDir + "padded/segementations", isoresampledFiles_segmentations, 10)
            paddedFiles_images = applyPadding(parentDir + "padded/images", isoresampledFiles_images, 10)

            """
            Apply center of mass alignment
            This function can handle both cases (processing only segmentation data or raw and segmentation data at the same time).
            """
            [comFiles_segmentations, comFiles_images] = applyCOMAlignment(parentDir + "com_aligned", paddedFiles_segmentations, paddedFiles_images, processRaw=True)

            """
            Apply centering
            """
            centerFiles_segmentations = center(parentDir + "centered", comFiles_segmentations)
            centerFiles_images = center(parentDir + "centered/images", comFiles_images)

            """
            Rigid alignment needs a reference file to align all the input files, FindReferenceImage function defines the median file as the reference.
            """
            medianFile = FindReferenceImage(centerFiles_segmentations)

            """
            Apply rigid alignment
            This function can handle both cases (processing only segmentation data or raw and segmentation data at the same time).
            This function uses the same transfrmation matrix for alignment of raw and segmentation files.
            """
            [alignedFiles_segmentations, alignedFiles_images] = applyRigidAlignment(parentDir + "aligned", medianFile,centerFiles_segmentations, centerFiles_images)

            # If user chose option 2, define cutting plane on median sample
            if choice == 2:
                input_file = medianFile.replace("centered", "aligned/segmentations").replace(".nrrd", ".aligned.DT.nrrd")
                cutting_plane_points = SelectCuttingPlane(input_file)

            elif choice == 1:
                postfix = "_femur.isores.pad.com.center.aligned.DT.nrrd"
                path = "aligned/segmentations/"
                input_file = parentDir + path + cp_prefix + postfix
                cutting_plane_points = SelectCuttingPlane(input_file)

                # catch for flipped norm
                if cutting_plane_points[0][1] < 0 and cutting_plane_points[1][1] < 0 and cutting_plane_points[2][1] < 0 :
                    cutting_plane_points[0][1] = cutting_plane_points[0][1] *-1
                    cutting_plane_points[1][1] = cutting_plane_points[1][1] *-1
                    cutting_plane_points[2][1] = cutting_plane_points[2][1] *-1

            print("Cutting plane points: ")
            print(cutting_plane_points)

            pickle.dump( [cutting_plane_points], open( outputDirectory + "groomed/groomed_pickle.p", "wb" ) )

            # Compute largest bounding box and apply cropping
            croppedFiles_segmentations = applyCropping(parentDir + "cropped/segmentations", alignedFiles_segmentations, alignedFiles_segmentations)
            croppedFiles_images = applyCropping(parentDir + "cropped/images", alignedFiles_images, alignedFiles_segmentations)

            groomed_segmentations = croppedFiles_segmentations


        # BEGIN GROOMING WITHOUT IMAGES
        else:
            """
            Reflect - We have left and right femurs, so we reflect the non-reference side meshes so that all of the femurs can be aligned
            """
            reflectedFiles_mesh = reflectMeshes(parentDir + 'reflected', files_mesh, reference_side)

            """
            MeshesToVolumes - Shapeworks requires volumes so we need to convert
            mesh segementaions to binary segmentations.
            """

            fileList_seg = MeshesToVolumes(parentDir + "volumes", reflectedFiles_mesh, spacing)

            """
            Apply isotropic resampling
            The segmentation and images are resampled independently to have uniform spacing.
            """
            isoresampledFiles_segmentations = applyIsotropicResampling(parentDir + "resampled/segmentations", fileList_seg, isBinary=True)

            """
            Apply padding
            Both the segmentation and raw images are padded in case the seg lies on the image boundary.
            """
            paddedFiles_segmentations = applyPadding(parentDir + "padded/segementations", isoresampledFiles_segmentations, 30)

            """
            Apply center of mass alignment
            This function can handle both cases (processing only segmentation data or raw and segmentation data at the same time).
            """
            comFiles_segmentations = applyCOMAlignment(parentDir + "com_aligned", paddedFiles_segmentations, None)

            """
            Apply centering
            """
            centerFiles_segmentations = center(parentDir + "centered/segmentations", comFiles_segmentations)

            """
            Rigid alignment needs a reference file to align all the input files, FindReferenceImage function defines the median file as the reference.
            """
            medianFile = FindReferenceImage(centerFiles_segmentations)

            """
            Apply rigid alignment
            This function can handle both cases (processing only segmentation data or raw and segmentation data at the same time).
            This function uses the same transfrmation matrix for alignment of raw and segmentation files.
            """
            alignedFiles_segmentations = applyRigidAlignment(parentDir + "aligned", medianFile,centerFiles_segmentations, None)

            # If user chose option 2, define cutting plane on median sample
            if choice == 2:
                input_file = medianFile.replace("centered/segmentations", "aligned").replace(".nrrd", ".aligned.DT.nrrd")
                cutting_plane_points = SelectCuttingPlane(input_file)

            elif choice == 1:
                postfix = "_femur.isores.pad.com.center.aligned.DT.nrrd"
                path = "aligned/"
                input_file = parentDir + path + cp_prefix + postfix
                cutting_plane_points = SelectCuttingPlane(input_file)

                # catch for flipped norm
                if cutting_plane_points[0][1] < 0 and cutting_plane_points[1][1] < 0 and cutting_plane_points[2][1] < 0 :
                    cutting_plane_points[0][1] = cutting_plane_points[0][1] *-1
                    cutting_plane_points[1][1] = cutting_plane_points[1][1] *-1
                    cutting_plane_points[2][1] = cutting_plane_points[2][1] *-1

            print("Cutting plane points: ")
            print(cutting_plane_points)

            pickle.dump( [cutting_plane_points], open( outputDirectory + "groomed/groomed_pickle.p", "wb" ) )

            groomed_segmentations = alignedFiles_segmentations


        print("\nStep 3. Groom - Convert to distance transforms\n")
        if args.interactive:
            input("Press Enter to continue")

        """
        We convert the scans to distance transforms, this step is common for both the
        prepped as well as unprepped data, just provide correct filenames.
        """
        dtFiles = applyDistanceTransforms(parentDir, groomed_segmentations)


    """
    ## OPTIMIZE : Particle Based Optimization

    Now that we have the distance transform representation of data we create
    the parameter files for the shapeworks particle optimization routine.
    For more details on the plethora of parameters for shapeworks please refer to
    '/Documentation/PDFs/ParameterDescription.pdf'

    We provide two different mode of operations for the ShapeWorks particle opimization;
    1- Single Scale model takes fixed number of particles and performs the optimization.
    For more detail about the optimization steps and parameters please refer to
    '/Documentation/PDFs/ScriptUsage.pdf'

    2- Multi scale model optimizes for different number of particles in hierarchical manner.
    For more detail about the optimization steps and parameters please refer to
    '/Documentation/PDFs/ScriptUsage.pdf'

    First we need to create a dictionary for all the parameters required by these
    optimization routines
    """
    print("\nStep 4. Optimize - Particle Based Optimization\n")

    if args.interactive:
        input("Press Enter to continue")

    pointDir = outputDirectory + 'shape_models/'
    if not os.path.exists(pointDir):
        os.makedirs(pointDir)

    # Cutting planes
    cutting_planes = []
    cutting_plane_counts = []
    for i in range(len(dtFiles)):
        cutting_planes.append(cutting_plane_points)
        cutting_plane_counts.append(1)


    parameterDictionary = {
        "number_of_particles" : 1024,
        "use_normals": 0,
        "normal_weight": 10.0,
        "checkpointing_interval" : 200,
        "keep_checkpoints" : 1,
        "iterations_per_split" : 4000,
        "optimization_iterations" : 4000,
        "starting_regularization" : 100,
        "ending_regularization" : 0.1,
        "recompute_regularization_interval" : 2,
        "domains_per_shape" : 1,
        "domain_type" : 'image',
        "relative_weighting" : 10,
        "initial_relative_weighting" : 1,
        "procrustes_interval" : 1,
        "procrustes_scaling" : 1,
        "save_init_splits" : 1,
        "debug_projection" : 0,
        "verbosity" : 2,
        "use_statistics_in_init" : 0,
        "adaptivity_mode": 0,
        "cutting_plane_counts": cutting_plane_counts,
        "cutting_planes": cutting_planes
    }
    if args.tiny_test:
        parameterDictionary["number_of_particles"] = 32
        parameterDictionary["optimization_iterations"] = 25
        parameterDictionary["iterations_per_split"] = 25

    if not args.use_single_scale:
        parameterDictionary["use_shape_statistics_after"] = 64

    """
    Now we execute the particle optimization function.
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

    Reconstruct the dense mean surface given the sparse correspondence model.
    """
    print("\nStep 5. Analysis - Reconstruct the dense mean surface given the sparse correspodence model.\n")
    if args.interactive:
        input("Press Enter to continue")

    launchShapeWorksStudio(pointDir, dtFiles, localPointFiles, worldPointFiles)
