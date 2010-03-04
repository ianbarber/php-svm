--TEST--
Load a larger amount of training data from a file and test regressions
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$data = array(
	"9" => array(1 => -1 ,2 => 0.418919 ,3 => 0.411765 ,4 => -0.637168 ,5 => -0.168408 ,6 => -0.294553 ,7 => -0.24424 ,8 => -0.389138 ),
	"10" => array(2 => 0.486486 ,3 => 0.445378 ,4 => -0.734513 ,5 => -0.226138 ,6 => -0.287155 ,7 => -0.314022 ,8 => -0.413054 ),
	"12" => array(1 => -1 ,2 => 0.716216 ,3 => 0.680672 ,4 => -0.654867 ,5 => 0.378785 ,6 => 0.270343 ,7 => -0.00987492 ,8 => -0.0164425),
);

$svm = new svm();

$svm->setOptions(array(
	SVM::OPT_TYPE => SVM::NU_SVC,
	SVM::OPT_KERNEL_TYPE => SVM::KERNEL_LINEAR,
	SVM::OPT_P => 0.1,  // epsilon 0.1
));
echo "training";
$model = $svm->train(dirname(__FILE__) . '/abalone.scale');
echo "trained";
if($model) {
	echo "ok train\n";
	return;
	foreach($data as $class => $d) {
		$result = $model->predict($d);
		if($result > 0) {
			echo "ok\n";
		} else {
			echo "regression failed: " . $result . "\n";
		}
	}
} else {
	echo "training failed";
}
?>
--EXPECT--
ok train
ok
ok
ok
