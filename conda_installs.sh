#
# Installs conda environment for building ShapeWorks
#
echo ""
echo "Note: this script only supports bash and zsh shells "
echo "      It must be called using \"source ./conda_installs.sh\""
echo ""

(return 0 2>/dev/null) && sourced=1 || sourced=0

if [[ "$sourced" == "0" ]]; then
  echo "ERROR: must call this script using \"source ./conda_installs.sh\""
  exit 1
fi

# PyTorch installation
function install_pytorch() {
  echo "installing pytorch"
  if [[ "$(uname)" == "Darwin" ]]; then
    pip install torch torchvision torchaudio
  elif ! [ -x "$(command -v nvidia-smi)" ]; then
    echo 'Could not find nvidia-smi, using cpu-only PyTorch'
    pip install torch==1.7.1+cpu torchvision==0.8.2+cpu torchaudio===0.7.2 -f https://download.pytorch.org/whl/torch_stable.html
  else
    CUDA=`nvidia-smi | grep CUDA | sed -e "s/.*CUDA Version: //" -e "s/ .*//"`
    echo "Found CUDA Version: ${CUDA}"
    if [[ "$CUDA" == "9.2" ]]; then
        pip install torch==1.7.1+cu92 torchvision==0.8.2+cu92 torchaudio==0.7.2 -f https://download.pytorch.org/whl/torch_stable.html
    elif [[ "$CUDA" == "10.1" ]]; then
        pip install torch==1.7.1+cu101 torchvision==0.8.2+cu101 torchaudio==0.7.2 -f https://download.pytorch.org/whl/torch_stable.html
    elif [[ "$CUDA" == "10.2" ]]; then
        pip install torch===1.7.1 torchvision===0.8.2 torchaudio===0.7.2 -f https://download.pytorch.org/whl/torch_stable.html
    elif [[ "$CUDA" == "11.0" || "$CUDA" == "11.1" || "$CUDA" == "11.2" ]]; then
        pip install torch===1.7.1+cu110 torchvision===0.8.2+cu110 torchaudio===0.7.2 -f https://download.pytorch.org/whl/torch_stable.html
    else
        echo "CUDA version not compatible, using cpu-only"
        pip install torch==1.7.1+cpu torchvision==0.8.2+cpu torchaudio===0.7.2 -f https://download.pytorch.org/whl/torch_stable.html
    fi
  fi
}

function install_conda() {
  if ! command -v conda 2>/dev/null 1>&2; then
    echo "installing anaconda..."
    if [[ "$(uname)" == "Darwin" ]]; then
      curl -o /tmp/Miniconda3-latest-MacOSX-x86_64.sh https://repo.anaconda.com/miniconda/Miniconda3-latest-MacOSX-x86_64.sh
      bash /tmp/Miniconda3-latest-MacOSX-x86_64.sh -b
      rm /tmp/Miniconda3-latest-MacOSX-x86_64.sh
    elif [[ "$(uname)" == "Linux" ]]; then
      curl -o ./Miniconda3-latest-Linux-x86_64.sh https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh
      bash ./Miniconda3-latest-Linux-x86_64.sh -b
      rm ./Miniconda3-latest-Linux-x86_64.sh
    else
      echo "ERROR: unknown OS $(uname)"
      return 1
    fi

    source ~/miniconda3/bin/activate
    conda config --set auto_activate_base false
  fi

  # add default channels
  conda config --add channels anaconda
  conda config --add channels conda-forge
  
  # update anaconda
  conda activate base
  if ! conda update --yes -n base conda; then return 1; fi
  if ! conda update --yes --all; then return 1; fi

  # create and activate shapeworks env
  CONDAENV=shapeworks
  if ! conda create --yes --name $CONDAENV python=3.7.8; then return 1; fi
  eval "$(conda shell.bash hook)"
  if ! conda activate $CONDAENV; then return 1; fi
  
  # pip is needed in sub-environments or the base env's pip will silently install to base
  if ! conda install --yes pip=20.2.3; then return 1; fi
  if ! python -m pip install --upgrade pip; then return 1; fi

  # install shapeworks deps
  if ! conda install --yes \
    cmake=3.18.2 \
    gtest=1.10.0 \
    colorama=0.4.3 \
    requests=2.24.0 \
    geotiff=1.6.0 \
    numpy=1.19.1 \
    git-lfs=2.11.0 \
    openblas=0.3.6 \
    doxygen=1.8.20 \
    graphviz=2.38.0 \
    vtk=8.2.0 \
    scikit-learn=0.21.3 \
    tbb=2019.9 \
    tbb-devel=2019.9 \
    boost=1.72.0 \
    openexr=2.5.3 \
    pybind11=2.5.0 \
    notebook=6.1.5 \
    nbformat=4.4.0 \
    pkg-config=0.29.2
  then return 1; fi

  # linux and mac (only) deps
  if [[ "$(uname)" == "Linux" || "$(uname)" == "Darwin" ]]; then
    if ! conda install --yes \
      xorg-libx11=1.6.12 \
      xorg-libsm=1.2.3 \
      libxrandr-devel-cos6-x86_64=1.5.1 \
      libxinerama-devel-cos6-x86_64=1.1.3 \
      libxcursor-devel-cos6-x86_64=1.1.14 \
      libxi-devel-cos6-x86_64=1.7.8 \
      openmp=8.0.1 \
      ncurses=6.2 \
      libuuid=2.32.1
    then return 1; fi
  fi

  if ! pip install trimesh;                             then return 1; fi
  if ! pip install termcolor==1.1.0;                    then return 1; fi
  if ! pip install grip==4.5.2;                         then return 1; fi
  if ! pip install matplotlib==3.3.2;                   then return 1; fi
  if ! pip install itk==5.0.1;                          then return 1; fi
  if ! pip install itkwidgets==0.32.0;                  then return 1; fi
  if ! pip install bokeh==2.2;                          then return 1; fi
  if ! pip install seaborn; then return 1; fi
  if ! pip install mdutils==1.3.0;                      then return 1; fi # lib for writing markdown files (auto-documentation)
  if ! pip install mkdocs==1.1.2;                       then return 1; fi # lib for generating documentation from markdown
  if ! pip install python-markdown-math==0.8;           then return 1; fi # lib for rendering equations in docs
  if ! pip install fontawesome-markdown==0.2.6;         then return 1; fi # lib for icons in documentation
  if ! pip install pymdown-extensions==8.0.1;           then return 1; fi # lib to support checkbox lists in documentation
  if ! pip install pyyaml==5.3.1;                       then return 1; fi # for mkdocs
  if ! pip install Python/DatasetUtilsPackage;          then return 1; fi # install the local GirderConnector code as a package
  if ! pip install Python/DocumentationUtilsPackage;    then return 1; fi # install shapeworks auto-documentation as a package
  if ! pip install Python/DataAugmentationUtilsPackage; then return 1; fi # install data augmentation code as a package
  if ! pip install Python/DeepSSMUtilsPackage;          then return 1; fi # install DeepSSM code as a package
  if ! pip install Python/ShapeCohortGenPackage;        then return 1; fi # install shape cohort generation code as a package


  if [[ "$GITHUB_ACTION" != "" ]]; then
      echo "Running under GitHub Action"
      pushd $HOME/miniconda3/envs/shapeworks/lib
      ls libffi*
      if [ ! -f libffi.6.dylib ]; then
	  ln -s libffi.7.dylib libffi.6.dylib
      fi
      popd
  fi

  
  # installs for jupyter notebooks

  if ! pip install nbstripout;         then return 1; fi # to stripout notebooks output before committing  
  if ! pip install pyvista;            then return 1; fi # for visualizations on notebooks
  if ! pip install ipyvtk_simple;      then return 1; fi # for visualizations on notebooks
  if ! pip install ipywidgets;         then return 1; fi # for visualizations on notebooks
  if ! pip install itkwidgets;         then return 1; fi # for visualizations on notebooks
  if ! pip install mkdocs-jupyter;     then return 1; fi # for adding notebooks to our documentation (supports toc and excutation before deployment)

  # for spell check markdown cells in jupyter notebooks and table of contents (toc2)
  conda install --yes jupyter_contrib_nbextensions
  jupyter contrib nbextension install --user
  jupyter nbextension enable spellchecker/main
  jupyter nbextension enable toc2/main

  # installing nbstripout to strip out notebooks cell outputs before committing 
  nbstripout --install
  nbstripout --install --attributes .gitattributes

  # install any additional Linux dependencies
  if [[ "$(uname)" == "Linux" ]]; then
    echo "nothing additional to install for Linux"
  fi

  conda info

  echo "Installed packages:"
  conda list
  
  return 0
}

if install_conda; then
  install_pytorch
  echo "$CONDAENV environment successfully created/updated!"
  conda activate $CONDAENV
else
  echo "Problem encountered creating/updating $CONDAENV conda environment."
  return 1;
fi
