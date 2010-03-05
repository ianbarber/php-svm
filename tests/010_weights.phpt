--TEST--
Test training with some unbalanced weighting
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$svm->setOptions(array(SVM::OPT_TYPE => SVM::NU_SVC));
try {
	$model = $svm->train(dirname(__FILE__) . '/australian.scale', array(1 => 1, -1 => 0.5));
} catch(SVMException $e) {
	echo "got exception\n";
}

$svm->setOptions(array(SVM::OPT_TYPE => SVM::C_SVC));
$model = $svm->train(dirname(__FILE__) . '/australian.scale', array(1 => 1, -1 => 0.5));

try {
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
	$result = $model->predict($data);
	if($result > 0) {
		echo "ok";
	}
} catch (SvmException $e) {
	echo $e->getMessage();
}

?>
--EXPECT--
got exception
ok