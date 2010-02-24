--TEST--
Make a prediction based on the model 
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$result = $svm->load(dirname(__FILE__) . '/australian.model');
if($result) {
	$data = array(
		"1" => 1,
		2 => -0.731729,
		3 => -0.886786,
		4 => -1,
		5 => 0.230769,
		"6" => -0.25,
		7 => -0.783509,
		8 => 1,
		9 => 1, 
		10 => "-0.820896",
		11 => -1, 
		13 => -0.92,
		"14" => "-1"
	);
	$result = $svm->predict($data);
	if($result > 0) {
		echo "ok";
	} else {
		echo "predict failed: $result";
	}
} else {
	echo "loading failed";
}
?>
--EXPECT--
ok
