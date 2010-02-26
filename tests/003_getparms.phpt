--TEST--
Test retrieving the SVM training parameters. 
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$params = $svm->getOptions();
if(count($params) > 2) {
	echo "ok 1\n";
} else {
	echo "retrieving params failed";
}

if(isset($params[SVM::OPT_CACHE_SIZE])) {
	echo "ok 2\n";
} else {
	echo "missing cache size";
}

if($params[SVM::OPT_CACHE_SIZE] == 100) {
	echo "ok 3\n";
} else {
	echo "invalid cache size";
}
?>
--EXPECT--
ok 1
ok 2
ok 3

