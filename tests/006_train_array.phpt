--TEST--
Train from an array
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$result = $svm->train(array(array(1, -13 => 1.33)));
var_dump($result);

try {
	$svm->train(array(array(1)));

} catch (SvmException $e) {
	echo "got exception";
}

?>
--EXPECTF--
object(svmmodel)#%d (%d) {
}
got exception
