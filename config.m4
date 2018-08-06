PHP_ARG_WITH(svm, whether to enable svm support,
[  --with-svm[=DIR]       Enable svn support. DIR is the prefix to libsvm installation directory.], yes)

if test "$PHP_SVM" != "no"; then


dnl Get PHP version depending on shared/static build

  AC_MSG_CHECKING([PHP version is at least 5.2.0])

  if test -z "${PHP_VERSION_ID}"; then
    if test -z "${PHP_CONFIG}"; then
      AC_MSG_ERROR([php-config not found])
    fi
    PHP_SVM_FOUND_VERNUM=`${PHP_CONFIG} --vernum`;
    PHP_SVM_FOUND_VERSION=`${PHP_CONFIG} --version`
  else
    PHP_SVM_FOUND_VERNUM="${PHP_VERSION_ID}"
    PHP_SVM_FOUND_VERSION="${PHP_VERSION}"
  fi

  if test "$PHP_SVM_FOUND_VERNUM" -ge "50200"; then
    AC_MSG_RESULT(yes. found $PHP_SVM_FOUND_VERSION)
  else 
    AC_MSG_ERROR(no. found $PHP_SVM_FOUND_VERSION)
  fi

  AC_MSG_CHECKING([for svm.h header])
  for i in $PHP_SVM /usr/local /usr;
  do
    test -r $i/include/svm.h && SVM_PREFIX=$i && SVM_INC_DIR=$i/include/ && SVM_OK=1
  done

  if test "$SVM_OK" != "1"; then
    for i in $PHP_SVM /usr/local /usr;
    do
      test -r $i/include/libsvm/svm.h && SVM_PREFIX=$i && SVM_INC_DIR=$i/include/libsvm/ && SVM_OK=1
    done
  fi
    
  if test "$SVM_OK" != "1"; then
    for i in $PHP_SVM /usr/local /usr;
    do
      test -r $i/include/libsvm-2.0/libsvm/svm.h && SVM_PREFIX=$i && SVM_INC_DIR=$i/include/libsvm-2.0/libsvm/ && SVM_OK=1
    done
  fi
  
  if test "$SVM_OK" != "1"; then
    AC_MSG_RESULT([not found, using bundled libsvm])

    PHP_REQUIRE_CXX()
    PHP_ADD_LIBRARY(stdc++,,SVM_SHARED_LIBADD)

    PHP_NEW_EXTENSION(svm, svm.c, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1, cxx)
    PHP_ADD_SOURCES_X(PHP_EXT_DIR(svm), libsvm/svm.cpp, -std=c++14, shared_objects_svm, yes)

    PHP_ADD_INCLUDE($ext_srcdir/libsvm)
    PHP_ADD_INCLUDE($ext_builddir/libsvm)
  else
  
    AC_MSG_RESULT([found in $SVM_INC_DIR])
  
    AC_MSG_CHECKING([for libsvm shared libraries])
    PHP_CHECK_LIBRARY(svm, svm_train, [
      PHP_ADD_LIBRARY_WITH_PATH(svm, $SVM_PREFIX/lib, SVM_SHARED_LIBADD)
      PHP_ADD_INCLUDE($SVM_INC_DIR)
    ],[
      AC_MSG_ERROR([not found. Make sure that libsvm is installed])
    ],[
      SVM_SHARED_LIBADD -lsvm
    ])
  
    PHP_NEW_EXTENSION(svm, svm.c, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)
  fi
  AC_DEFINE(HAVE_SVM,1,[ ])

  PHP_SUBST(SVM_SHARED_LIBADD)
fi

