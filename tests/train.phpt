--TEST--
Load training data from a file. 
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$result = $svm->train(dirname(__FILE__) . '/australian.scale');
if($result) {
	if($svm->save(dirname(__FILE__) . '/australian.model')) {
		echo "ok";
	} else {
		echo "model save failure";
	}
	
} else {
	echo "training failed";
}
?>
--EXPECT--
ok
