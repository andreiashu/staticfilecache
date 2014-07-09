<?php

require_once __DIR__ . '/DrupalStaticFileCache.inc';

class DrupalStaticFileCacheTest extends PHPUnit_Framework_TestCase {

  const BIN = 'TBIN';
  const WHITELISTED_CID = 'whitelisted_cid';
  const VALUE_FROM_FALLBACK_CACHE = 'VALUE_FROM_FALLBACK_CACHE';

  public function testConstructor() {
    $methods = $this->getMockedMethods();
    $mock = $this->getMockedCache($methods);

    $mock->expects($this->once())
      ->method('getCacheObjectFromCid')
      ->with(self::WHITELISTED_CID)
      ;

    $this->assertEquals('TEST_VALUE', $mock->get(self::WHITELISTED_CID));
    $this->assertEquals(self::VALUE_FROM_FALLBACK_CACHE, $mock->get('non_whitelisted'));
  }

  public function testGetNotAllowedWithCidWhitelisted() {
    $fallback_mock = $this->getMockedFallback();
    $fallback_mock->expects($this->never())
      ->method('get')
      ;
    $methods = $this->getMockedMethods(array(
      'isGetAllowed' => FALSE,
      'isCidWhitelisted' => TRUE,
      'getFallbackCache' => $fallback_mock,
    ));
    $mock = $this->getMockedCache($methods);

    $mock->expects($this->never())
      ->method('getCacheObjectFromCid')
      ;

    $this->assertEquals(FALSE, $mock->get(self::WHITELISTED_CID));
    $this->assertEquals(FALSE, $mock->get('non_whitelisted'));
  }

  public function testGetWithCidWhitelisted() {
    $fallback_mock = $this->getMockedFallback();
    $fallback_mock->expects($this->once())
      ->method('get')
      ->will($this->returnValue(self::VALUE_FROM_FALLBACK_CACHE))
      ;
    $methods = $this->getMockedMethods(array(
      'isGetAllowed' => FALSE,
      'isCidWhitelisted' => FALSE,
      'getFallbackCache' => $fallback_mock,
    ));
    $mock = $this->getMockedCache($methods);
    $mock->expects($this->never())
      ->method('getCacheObjectFromCid')
      ;

    $this->assertEquals(self::VALUE_FROM_FALLBACK_CACHE, $mock->get(self::WHITELISTED_CID));
  }

  public function testSetWithCidWhitelisted() {
    $fallback_mock = $this->getMockedFallback();
    $fallback_mock->expects($this->never())
      ->method('set')
    ;
    $methods = $this->getMockedMethods(array(
      'getFallbackCache' => $fallback_mock,
      'isCidWhitelisted' => TRUE
    ));
    $mock = $this->getMockedCache($methods);

    $mock->expects($this->once())
      ->method('isUpdateAllowed')
      ->will($this->returnValue(FALSE))
    ;

    $mock->expects($this->once())
      ->method('isAddAllowed')
      ->will($this->returnValue(FALSE))
    ;

    $mock->expects($this->never())
      ->method('getFilepathFromCid')
      ;

    $this->assertEquals(FALSE, $mock->set(self::WHITELISTED_CID, array(), 0));
  }

  public function testSetWithCidNotWhitelisted() {
    $dummy_data = array('dummy' => 'data');

    $fallback_mock = $this->getMockedFallback();
    $fallback_mock->expects($this->once())
      ->method('set')
      ->with(self::WHITELISTED_CID, $dummy_data, 0)
      ->will($this->returnValue('DUMMY'))
    ;
    $methods = $this->getMockedMethods(array(
      'getFallbackCache' => $fallback_mock,
      'isCidWhitelisted' => FALSE
    ));
    $mock = $this->getMockedCache($methods);

    $mock->expects($this->never())
      ->method('isUpdateAllowed')
    ;
    $mock->expects($this->never())
      ->method('getFilepathFromCid')
    ;

    $this->assertEquals('DUMMY', $mock->set(self::WHITELISTED_CID, $dummy_data, 0));
  }

  /**
   * @param array $methods
   * @return PHPUnit_Framework_MockObject_MockObject
   */
  private function getMockedCache($methods) {
    $mock = $this->getMockBuilder('\DrupalStaticFileCache')
      ->setConstructorArgs(array(self::BIN))
      ->setMethods(array_keys($methods))
      ->getMock()
    ;

    foreach($methods as $method => $return_value) {
      $mock->expects($this->any())
        ->method($method)
        ->will($this->returnValue($return_value))
      ;
    }

    return $mock;
  }

  /**
   * @return PHPUnit_Framework_MockObject_MockObject
   */
  private function getMockedFallback() {
    $fallback_mock = $this->getMockBuilder('\stdClass')
      ->setMethods(array('get', 'set', 'clear'))
      ->getMock()
    ;

    return $fallback_mock;
  }

  private function getMockedMethods($overrides = array()) {
    $fallback_mock = $this->getMockedFallback();
    $fallback_mock->expects($this->any())
      ->method('get')
      ->will($this->returnValue(self::VALUE_FROM_FALLBACK_CACHE))
      ;

    return $overrides + array(
      'isAddAllowed' => FALSE,
      'isUpdateAllowed' => FALSE,
      'isDeleteAllowed' => FALSE,
      'isGetAllowed' => TRUE,
      'getUpdateIgnoreKeys' => array(''),
      'getFallbackCacheClass' => 'DrupalFakeCache',
      'getFallbackCache' => $fallback_mock,
      'getWhitelistCids' => array(self::BIN . '-' . self::WHITELISTED_CID),
      'getCacheDirectorySetting' => './test',
      'getCacheObjectFromCid' => 'TEST_VALUE',
    );
  }
}

/**
 * Copy of DrupalCacheInterface. This allows the unit test to run
 * without having to include any drupal dependencies.
 */
interface DrupalCacheInterface {

  function get($cid);

  function getMultiple(&$cids);

  function set($cid, $data, $expire = CACHE_PERMANENT);

  function clear($cid = NULL, $wildcard = FALSE);

  function isEmpty();
}
