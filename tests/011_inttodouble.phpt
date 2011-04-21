--TEST--
Train from an array, testing conversion of ints to doubles
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$result = $svm->train(array(array(1, 1 => 1), array(-1, 1 => -1)));
echo $result->predict(array(0, 1 => -1)), PHP_EOL;
echo $result->predict(array(0, 1 => 1));


?>
--EXPECTF--
-1
1