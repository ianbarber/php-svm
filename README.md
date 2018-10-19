[![Build Status](https://travis-ci.com/ianbarber/php-svm.svg?branch=master)](https://travis-ci.com/ianbarber/php-svm) (Travis)

[![Windows Build Status](https://ci.appveyor.com/api/projects/status/2mbbc10n87dvw526?svg=true)](https://ci.appveyor.com/project/ianbarber/php-svm) (AppVeyor)

# INTRODUCTION

Available via PECL: http://pecl.php.net/package/svm
Documentation at: http://php.net/manual/en/book.svm.php

LibSVM is an efficient solver for SVM classification and regression problems. The svm extension wraps this in a PHP interface for easy use in PHP scripts.
As of version 0.2.0 the extension requires PHP 7.0 or above. Older versions are compatible with PHP from 5.2 to 5.6.

# INSTALLATION

Libsvm itself is required. A bundled libsvm version is delivered as of version 0.2.0 and will be used when libsvm is not available on the target system. An external libsvm is usually available through some package management or can be compiled from source when fetched directly from the website. 

On ubuntu and other debian based systems, you may be able to installed the `libsvm-dev` package:

    apt-get install libsvm-dev

If installing from the [website](http://www.csie.ntu.edu.tw/~cjlin/libsvm) then some steps will need to be taken as the package does not install automatically:

    wget http://www.csie.ntu.edu.tw/~cjlin/cgi-bin/libsvm.cgi?+http://www.csie.ntu.edu.tw/~cjlin/libsvm+tar.gz 
    tar xvzf libsvm-3.1.tar.gz 
    cd libsvm-3.1 
    make lib 
    cp libsvm.so.1 /usr/lib 
    ln -s libsvm.so.1 libsvm.so 
    ldconfig 
    ldconfig --print | grep libsvm

This last step should show libsvm is installed.

Once libsvm is installed, the extension can be installed in the usual way.

# INSTALLING ON WINDOWS

A prebuilt win32 DLL is available from https://pecl.php.net/package/svm. The latest development snapshots are also fetcheable from AppVeyor artifacts.

The extension builds properly on Windows, if following the Windows step by step build process: https://wiki.php.net/internals/windows/stepbystepbuild 

0. Put SVM source in pecl/svm in phpdev directory
0. Download latest libsvm, rename Makefile to Makefile.old and Makefile.win to Makefile - nmake 
0. Copy libsvm.lib from libsvm windows directory, and into deps/libs
0. Copy svm.h into `includes/libsvm/`
```
buildconf
configure --disable-all --enable-cli --with-svm=shared
nmake
```
Note, that if the bundled libsvm is used, the instructions about the libsvm setup can be ommited. Otherwise, make sure the libsvm.dll file from the libsvm windows directory in the path when running the resulting lib. 

# USAGE

The basic process is to define parameters, supply training data to generate a model on, then make predictions based on the model. There are a default set of parameters that should get some results with most any input, so we'll start by looking at the data. 

Data is supplied in either a file, a stream, or as an array. If supplied in a file or a stream, it must contain one line per training example, which must be formatted as an integer class (usually 1 and -1) followed by a series of feature/value pairs, in increasing feature order. The features are integers, the values floats, usually scaled 0-1. For example:

    -1 1:0.43 3:0.12 9284:0.2

In a document classification problem, say a spam checker, each line would represent a document. There would be two classes, -1 for spam, 1 for ham. Each feature would represent some word, and the value would represent that importance of that word to the document (perhaps the frequency count, with the total scaled to unit length). Features that were 0 (e.g. the word did not appear in the document at all) would simply not be included.  

In array mode, the data must be passed as an array of arrays. Each sub-array must have the class as the first element, then key => value sets for the feature values pairs. E.g.

    array(
    	array(-1, 1 => 0.43, 3 => 0.12, 9284 => 0.2),
    );

This data is passed to the SVM class's train function, which will return an SVM model is successful. 

    $svm = new SVM();
    $model = $svm->train($data);

or, for example with a filename

    $svm = new SVM();
    $model = $svm->train("traindata.txt");

Once a model has been generated, it can be used to make predictions about previously unseen data. This can be passed as an array to the model's predict function, in the same format as before, but without the label. The response will be the class. 

    $data = array(1 => 0.43, 3 => 0.12, 9284 => 0.2);
    $result = $model->predict($data);

In this case, $result would be -1. 

Models can be saved and restored as required, using the save and load functions, which both take a file location. 

    $model->save('model.svm');

And to load: 

    $model = new SVMModel();
    $model->load('model.svm');
