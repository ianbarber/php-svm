--TEST--
Load training data from a stream. 
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$fh = fopen(dirname(__FILE__) . '/australian.scale', 'r');
$svm = new svm();
$result = $svm->train($fh);
fclose($fh);

try {
	$result->save(dirname(__FILE__) . '/australian.model');
	echo "ok";
} catch (SvmException $e) {
	echo $e->getMessage();
}

?>
--EXPECT--
ok
