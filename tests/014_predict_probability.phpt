--TEST--
Get the prediction probability based on the model 
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
$svm->setOptions(array(
	SVM::OPT_TYPE => SVM::C_SVC,
	SVM::OPT_KERNEL_TYPE => SVM::KERNEL_LINEAR,
	SVM::OPT_P => 0.1,  // epsilon 0.1
	SVM::OPT_PROBABILITY => 1
));
$model = $svm->train(dirname(__FILE__) . '/abalone.scale');

if($model) {
	$data = array(
		1 => -1,
		2 => 0.027027,
		3 => 0.0420168,
		4 => -0.831858,
		5 => -0.63733,
		6 => -0.699395,
		7 => -0.735352,
		8 => -0.704036
	);
	$class = $model->predict($data);
	$return = array();
	$result = $model->predict_probability($data, $return);
    arsort($return);
    reset($return);
    $key = key($return);
	if($class == 9 && $key == 9) {
		echo "ok";
	} else {
		echo "predict failed: $class $result";
	}
} else {
	echo "loading failed";
}
?>
--EXPECT--
ok
