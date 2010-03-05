--TEST--
Test setOptions
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();

$options = array(Svm::OPT_TYPE => SVM::NU_SVR, Svm::OPT_COEF_ZERO => 1.2);

$svm->setOptions($options);

var_dump($options[Svm::OPT_TYPE] == SVM::NU_SVR);

echo "ok\n";

$options = array(Svm::OPT_TYPE => 31337);
try {
	$svm->setOptions($options);
} catch (SVMException $e) {
	echo "got exception";
}
?>
--EXPECT--
bool(true)
ok
got exception