--TEST--
Test 5 fold cross validation 
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$result = $svm->crossvalidate(dirname(__FILE__) . '/australian.scale', 5);
if($result > 0) {
	echo "ok";
}
echo $result;

?>
--EXPECT--
ok
