<?php

/**
 * Uses the filesystem as a caching storage
 * It allows to set a fallback cache class that will be used for CIDs that are
 * not in the whitelist
 */
class DrupalStaticFileCache implements DrupalCacheInterface {

  /**
   * Maximum filename length
   */
  const MAX_FILENAME_LENGTH = 254;

  /**
   * Where the cache files should be stored
   * @var string
   */
  private $directory;

  /**
   * @var string
   */
  private $prefix;

  /**
   * @var \DrupalCacheInterface
   */
  private $fallbackCache;

  /**
   * @param $bin
   */
  public function __construct($bin) {
    $this->bin = $bin;

    $this->directory = $this->getCacheDirectorySetting();
    $this->prefix = $this->directory .'/'. $this->getPrefixedCid('');
  }

  /**
   * @param string $raw_cid
   * @return bool|mixed
   */
  public function get($raw_cid) {
    $cid = $this->encodeCid($raw_cid);
    if (!$this->isCidWhitelisted($cid)) {
      return $this->getFallbackCache()->get($raw_cid);
    }

    // gets have been disabled
    if (!$this->isGetAllowed()) {
      return FALSE;
    }

    return $this->getCacheObjectFromCid($raw_cid);
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids) {
    $results = array();
    foreach ($cids as $cid) {
      $cache = $this->get($cid);
      if ($cache !== FALSE) {
        $results[$cid] = $cache;
        unset($cids[$cid]);
      }
    }
    return $results;
  }

  /**
   * @param $raw_cid
   * @return bool|mixed
   */
  protected function getCacheObjectFromCid($raw_cid) {
    $cid = $this->encodeCid($raw_cid);

    $filename = $this->getFilepathFromCid($cid);

    if (!@is_file($filename)) {
      return FALSE;
    }

    // Use @ because cache entry may not exist
    $content = @file_get_contents($filename);
    if ($content === FALSE) {
      return FALSE;
    }

    return @unserialize($content);
  }

  /**
   * @param $raw_cid
   * @param $data
   * @param int $expire
   * @return mixed
   */
  public function set($raw_cid, $data, $expire = CACHE_PERMANENT) {
    $cid = $this->encodeCid($raw_cid);

    if (!$this->isCidWhitelisted($cid)) {
      return $this->getFallbackCache()->set($raw_cid, $data, $expire);
    }

    // do not set cache if the settings do not allow us to update or add new
    // cache entries
    if (!$this->isUpdateAllowed() && !$this->isAddAllowed()) {
      return FALSE;
    }

    $filename = $this->getFilepathFromCid($cid);

    // if we're not allowed to add a new file return
    if (!$this->isAddAllowed() && !file_exists($filename)) {
      return FALSE;
    }

    $cache = new StdClass;
    $cache->cid = $cid;
    $cache->created = REQUEST_TIME;
    $cache->expire = $expire;
    $cache->data = $data;

    $ignored_keys = $this->getUpdateIgnoreKeys();
    if (!empty($ignored_keys) && $this->cacheDataIdentical($raw_cid, $cache, $ignored_keys)) {
      return FALSE;
    }

    // Open file for that entry, handling errors that may arise
    $fh = @fopen($filename, 'c+b');
    if ($fh === FALSE) {
      // most likely permission error - report it as critical error
      $this->logCritical(
        'Cannot open %filename',
        array('%filename' => $filename)
      );
      return;
    }

    // safeguard for simultaneous writing in the same file
    if (flock($fh, LOCK_EX) === FALSE) {
      $this->logCritical(
        'Cannot obtain exclusive lock for file %filename',
        array('%filename' => $filename)
      );
      fclose($fh);
      return;
    }

    if (ftruncate($fh, 0) === FALSE ||
        fwrite($fh, serialize($cache)) === FALSE ||
        flock($fh, LOCK_UN) === FALSE ||
        fclose($fh) === FALSE) {
      $this->logCritical(
        'Cannot write file %filename',
        array('%filename' => $filename)
      );

      // XXX should not happen -> cleanup
      unlink($filename);
      flock($fh, LOCK_UN);
      fclose($fh);
      return;
    }
  }

  /**
   * @param null $cid
   * @param bool $wildcard
   */
  public function clear($cid = NULL, $wildcard = FALSE) {
    global $user;

    if (!$this->isCidWhitelisted($cid)) {
      return $this->getFallbackCache()->clear($cid, $wildcard);
    }

    if (!$this->isDeleteAllowed()) {
      return FALSE;
    }

    if (empty($cid)) {
      if ($this->getCacheLifetime()) {
        // We store the time in the current user's $user->cache variable which
        // will be saved into the sessions bin by _drupal_session_write(). We then
        // simulate that the cache was flushed for this user by not returning
        // cached data that was cached before the timestamp.
        $user->cache = REQUEST_TIME;

        $cache_flush = $this->getBinCacheFlush();
        if ($cache_flush == 0) {
          // This is the first request to clear the cache, start a timer.
          variable_set('cache_flush_' . $this->bin, REQUEST_TIME);
        }
        elseif (REQUEST_TIME > ($cache_flush + $this->getCacheLifetime())) {
          // Clear the cache for everyone, cache_lifetime seconds have
          // passed since the first request to clear the cache.
          $this->delete_expired();
          variable_set('cache_flush_' . $this->bin, 0);
        }
      }
      else {
        // No minimum cache lifetime, flush all temporary cache entries now.
        $this->delete_expired();
      }
    }
    else {
      if ($wildcard) {
        if ($cid == '*') {
          $this->delete_wildcard('');
        }
        else {
          $this->delete_wildcard($cid);
        }
      }
      elseif (is_array($cid)) {
        foreach ($cid as $one_cid) {
          $this->delete_one($one_cid);
        }
      }
      else {
        $this->delete_one($cid);
      }
    }
  }

  /**
   * @return bool
   */
  public function isEmpty() {
    return count($this->all()) == 0;
  }

  /**
   * Compares an old cache identified by $raw_cid with the $new_cache object
   *
   * @param $raw_cid
   * @param $new_cache
   * @param array $ignored_keys
   *   If specified it will ignore these keys when doing the comparison
   * @return bool
   */
  public function cacheDataIdentical($raw_cid, $new_cache, $ignored_keys = array()) {
    $cached_object = $this->getCacheObjectFromCid($raw_cid);
    if (FALSE === $cached_object) {
      return FALSE;
    }

    foreach($ignored_keys as $key) {
      if (isset($new_cache->{$key})) {
        $cached_object->{$key} = $new_cache->{$key};
      }
      else {
        unset($cached_object->{$key});
      }
    }

    return serialize($cached_object) === serialize($new_cache);
  }

  /**
   * @return string
   */
  public function getCacheDirectorySetting() {
    return variable_get('cache_staticfile_cache_dir', NULL);
  }

  /**
   * @param string $cid
   */
  protected function delete_one($cid) {
    $filename = $this->getFilepathFromCid($cid);
    @unlink($filename);
  }

  protected function all($cid_prefix = '') {
    $list = array();
    $filename_prefix = $this->getPrefixedCid($this->encodeCid($cid_prefix));
    $filename_prefix_len = strlen($filename_prefix);
    $cwd = getcwd();
    chdir($this->directory);
    $dh = opendir('.');
    while (($filename = readdir($dh)) !== FALSE) {
      if (strncmp($filename, $filename_prefix, $filename_prefix_len) === 0) {
        $list[] = $filename;
      }
    }
    closedir($dh);
    chdir($cwd);
    return $list;
  }

  protected function delete_wildcard($cid_prefix) {
    foreach ($this->all($cid_prefix) as $filename) {
      @unlink ($this->directory . '/' . $filename);
    }
  }

  protected function delete_expired() {
    $cwd = getcwd();
    chdir($this->directory);
    foreach ($this->all() as $filename) {
      if (!@is_file($filename)) {
        continue;
      }
      $content = @file_get_contents($filename);
      if ($content === FALSE) {
        continue;
      }
      $cache = @unserialize($content);
      if ($content === FALSE) {
        continue;
      }
      if ($cache->expire == CACHE_PERMANENT) {
        continue;
      }
      if ($cache->expire == CACHE_TEMPORARY ||
          $cache->expire < REQUEST_TIME) {
        @unlink($filename);
      }
    } // foreach $filename
    chdir($cwd);
  }

  /**
   * @param $cid
   * @return string
   */
  public function encodeCid($cid) {
    return substr(preg_replace('/[^0-9a-zA-Z\_\-\.]/', '_', $cid), 0, self::MAX_FILENAME_LENGTH);
  }

  /**
   * @param string $cid
   * @return string
   */
  protected function getFilepathFromCid($cid) {
    return $this->prefix . $cid;
  }

  /**
   * @return bool
   */
  protected function isAddAllowed() {
    return (bool) variable_get('cache_staticfile_add_allowed', FALSE);
  }

  /**
   * @return bool
   */
  protected function isUpdateAllowed() {
    return (bool) variable_get('cache_staticfile_update_allowed', FALSE);
  }

  /**
   * @return bool
   */
  protected function isDeleteAllowed() {
    return (bool) variable_get('cache_staticfile_delete_allowed', FALSE);
  }

  /**
   * @return bool
   */
  protected function isGetAllowed() {
    return (bool) variable_get('cache_staticfile_get_allowed', TRUE);
  }

  /**
   * @return null
   */
  protected function getCacheLifetime() {
    return variable_get('cache_lifetime', 0);
  }

  /**
   * @return null
   */
  protected function getBinCacheFlush() {
    return variable_get('cache_flush_' . $this->bin, 0);
  }

  /**
   * @return array
   */
  protected function getUpdateIgnoreKeys() {
    return (array) variable_get('cache_staticfile_update_ignore_keys', array());
  }

  /**
   * @return string
   */
  protected function getFallbackCacheClass() {
    return variable_get('cache_staticfile_fallback', 'DrupalFakeCache');
  }

  /**
   * @return DrupalCacheInterface
   */
  public function getFallbackCache() {
    if (!isset($this->fallbackCache)) {
      $fallback_cache_class = $this->getFallbackCacheClass();
      $this->fallbackCache = new $fallback_cache_class($this->bin);
    }

    return $this->fallbackCache;
  }

  /**
   * @param string $cid
   * @return bool
   */
  protected function isCidWhitelisted($cid) {
    $exists = in_array($this->getPrefixedCid($cid), $this->getWhitelistCids());
    if ($exists) {
      return $exists;
    }

    return $this->isCidRegexWhitelisted($cid);
  }

  /**
   * @param $cid
   * @return bool
   */
  protected function isCidRegexWhitelisted($cid) {
    $whitelist_pattern = $this->getWhitelistCidPatterns();

    foreach($whitelist_pattern as $pattern) {
      if (preg_match($pattern, $this->getPrefixedCid($cid))) {
        return TRUE;
      }
    }

    return FALSE;
  }

  /**
   * @return array
   */
  protected function getWhitelistCids() {
    return variable_get('cache_staticfile_whitelist_cids', array());
  }

  /**
   * @return array
   */
  protected function getWhitelistCidPatterns() {
    $whitelist = $this->getWhitelistCids();

    $patterns = array();
    foreach($whitelist as $pattern) {
      if (0 === strpos($pattern, '/')) {
        $patterns[] = $pattern;
      }
    }

    return $patterns;
  }

  /**
   * @param string $cid
   * @return string
   */
  protected function getPrefixedCid($cid) {
    return $this->bin . '-' . $cid;
  }

  /**
   * Proxy method for watchdog
   *
   * @param $message
   * @param array $variables
   * @param int $severity
   * @param null $link
   */
  protected function log($message, $variables = array(), $severity = NULL) {
    watchdog('staticfilecache', $message, $variables, isset($severity) ?: WATCHDOG_NOTICE);
  }

  /**
   * Log a critical error
   *
   * @param $message
   * @param array $variables
   */
  protected function logCritical($message, $variables = array()) {
    $this->log($type, $message, $variables, WATCHDOG_CRITICAL);
  }
}
