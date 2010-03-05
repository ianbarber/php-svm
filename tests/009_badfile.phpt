--TEST--
Test handling a bad filename
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();
try {
	@$svm->train('bad.file'); // shutup op to stop the warning which has current path in it
} catch(SVMException $e) {
	echo "got exception";
}
?>
--EXPECT--
got exception
