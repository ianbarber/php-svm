--TEST--
Test handling a bad filename
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
try {
	$svm->train(dirname(__FILE__) . '/baddata.scale'); 
} catch(SVMException $e) {
	echo "got exception";
}
?>
--EXPECT--
got exception
