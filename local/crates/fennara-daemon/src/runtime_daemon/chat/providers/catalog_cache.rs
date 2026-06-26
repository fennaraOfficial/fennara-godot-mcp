use reqwest::Client;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::sync::{OnceLock, RwLock};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use tokio::sync::Mutex;

use super::models_dev::{
    OpenRouterCatalog, parse_deepseek_catalog, parse_lmstudio_catalog, parse_ollama_cloud_catalog,
    parse_openrouter_catalog, parse_zai_catalog,
};
use crate::runtime_daemon::chat::settings;

pub(crate) const DEFAULT_MODELS_DEV_URL: &str =
    "https://www.fennara.io/catalog/models-dev/api.json";
const CACHE_FILE: &str = "models-dev-api.v1.json";
const META_FILE: &str = "models-dev-api.v1.meta.json";
const DEFAULT_TTL: Duration = Duration::from_secs(6 * 60 * 60);
const FETCH_TIMEOUT: Duration = Duration::from_secs(10);
const MAX_RESPONSE_BYTES: u64 = 32 * 1024 * 1024;

static REFRESH_LOCK: OnceLock<Mutex<()>> = OnceLock::new();
static MEMORY_CACHE: OnceLock<RwLock<Option<CachedOpenRouterCatalog>>> = OnceLock::new();

#[derive(Clone, Debug)]
pub(crate) struct CachedOpenRouterCatalog {
    pub(crate) catalog: OpenRouterCatalog,
    pub(crate) ollama_cloud: OpenRouterCatalog,
    pub(crate) lmstudio: OpenRouterCatalog,
    pub(crate) deepseek: OpenRouterCatalog,
    pub(crate) zai: OpenRouterCatalog,
    pub(crate) meta: CatalogMeta,
    pub(crate) stale: bool,
}

#[derive(Clone, Debug)]
pub(crate) struct CatalogPaths {
    cache_file: PathBuf,
    meta_file: PathBuf,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub(crate) struct CatalogMeta {
    pub(crate) source_url: String,
    pub(crate) fetched_at_ms: u128,
    pub(crate) openrouter_model_count: usize,
    #[serde(default)]
    pub(crate) ollama_cloud_model_count: usize,
    #[serde(default)]
    pub(crate) lmstudio_model_count: usize,
    #[serde(default)]
    pub(crate) deepseek_model_count: usize,
    #[serde(default)]
    pub(crate) zai_model_count: usize,
}

impl CatalogMeta {
    pub(crate) fn age_ms(&self) -> Option<u128> {
        now_ms().checked_sub(self.fetched_at_ms)
    }
}

pub(crate) fn default_paths() -> CatalogPaths {
    let dir = settings::app_dir().join("catalog");
    CatalogPaths {
        cache_file: dir.join(CACHE_FILE),
        meta_file: dir.join(META_FILE),
    }
}

pub(crate) async fn load_disk() -> Result<CachedOpenRouterCatalog, String> {
    let loaded = load_disk_from(&default_paths()).await?;
    store_memory_cache(&loaded);
    Ok(loaded)
}

pub(crate) fn load_disk_blocking() -> Result<CachedOpenRouterCatalog, String> {
    if let Some(cached) = memory_cache() {
        return Ok(cached);
    }
    let loaded = load_disk_blocking_from(&default_paths())?;
    store_memory_cache(&loaded);
    Ok(loaded)
}

pub(crate) async fn refresh(force: bool) -> Result<CachedOpenRouterCatalog, String> {
    let loaded = refresh_with_paths(force, default_paths()).await?;
    store_memory_cache(&loaded);
    Ok(loaded)
}

pub(crate) fn spawn_refresh_if_stale() {
    tokio::spawn(async {
        let should_refresh = match load_disk().await {
            Ok(cached) => cached.stale,
            Err(_) => true,
        };
        if should_refresh {
            let _ = refresh(false).await;
        }
    });
}

async fn load_disk_from(paths: &CatalogPaths) -> Result<CachedOpenRouterCatalog, String> {
    if let Some(path) = std::env::var_os("FENNARA_MODELS_DEV_PATH") {
        let source = PathBuf::from(path);
        let bytes = tokio::fs::read(&source)
            .await
            .map_err(|error| format!("Failed to read {}: {error}", source.display()))?;
        let catalog = parse_openrouter_catalog(&bytes)?;
        let ollama_cloud = parse_ollama_cloud_catalog(&bytes).unwrap_or_default();
        let lmstudio = parse_lmstudio_catalog(&bytes).unwrap_or_default();
        let deepseek = parse_deepseek_catalog(&bytes).unwrap_or_default();
        let zai = parse_zai_catalog(&bytes).unwrap_or_default();
        let meta = CatalogMeta {
            source_url: source.display().to_string(),
            fetched_at_ms: now_ms(),
            openrouter_model_count: catalog.models.len(),
            ollama_cloud_model_count: ollama_cloud.models.len(),
            lmstudio_model_count: lmstudio.models.len(),
            deepseek_model_count: deepseek.models.len(),
            zai_model_count: zai.models.len(),
        };
        return Ok(CachedOpenRouterCatalog {
            catalog,
            ollama_cloud,
            lmstudio,
            deepseek,
            zai,
            meta,
            stale: false,
        });
    }

    let bytes = tokio::fs::read(&paths.cache_file)
        .await
        .map_err(|error| format!("Failed to read {}: {error}", paths.cache_file.display()))?;
    let catalog = parse_openrouter_catalog(&bytes)?;
    let ollama_cloud = parse_ollama_cloud_catalog(&bytes).unwrap_or_default();
    let lmstudio = parse_lmstudio_catalog(&bytes).unwrap_or_default();
    let deepseek = parse_deepseek_catalog(&bytes).unwrap_or_default();
    let zai = parse_zai_catalog(&bytes).unwrap_or_default();
    let meta = read_meta(paths).await.unwrap_or_else(|| CatalogMeta {
        source_url: source_url(),
        fetched_at_ms: file_modified_ms(&paths.cache_file).unwrap_or_default(),
        openrouter_model_count: catalog.models.len(),
        ollama_cloud_model_count: ollama_cloud.models.len(),
        lmstudio_model_count: lmstudio.models.len(),
        deepseek_model_count: deepseek.models.len(),
        zai_model_count: zai.models.len(),
    });
    let stale = !is_fresh(&meta, now_ms(), DEFAULT_TTL);
    Ok(CachedOpenRouterCatalog {
        catalog,
        ollama_cloud,
        lmstudio,
        deepseek,
        zai,
        meta,
        stale,
    })
}

fn load_disk_blocking_from(paths: &CatalogPaths) -> Result<CachedOpenRouterCatalog, String> {
    if let Some(path) = std::env::var_os("FENNARA_MODELS_DEV_PATH") {
        let source = PathBuf::from(path);
        let bytes = std::fs::read(&source)
            .map_err(|error| format!("Failed to read {}: {error}", source.display()))?;
        let catalog = parse_openrouter_catalog(&bytes)?;
        let ollama_cloud = parse_ollama_cloud_catalog(&bytes).unwrap_or_default();
        let lmstudio = parse_lmstudio_catalog(&bytes).unwrap_or_default();
        let deepseek = parse_deepseek_catalog(&bytes).unwrap_or_default();
        let zai = parse_zai_catalog(&bytes).unwrap_or_default();
        let meta = CatalogMeta {
            source_url: source.display().to_string(),
            fetched_at_ms: now_ms(),
            openrouter_model_count: catalog.models.len(),
            ollama_cloud_model_count: ollama_cloud.models.len(),
            lmstudio_model_count: lmstudio.models.len(),
            deepseek_model_count: deepseek.models.len(),
            zai_model_count: zai.models.len(),
        };
        return Ok(CachedOpenRouterCatalog {
            catalog,
            ollama_cloud,
            lmstudio,
            deepseek,
            zai,
            meta,
            stale: false,
        });
    }

    let bytes = std::fs::read(&paths.cache_file)
        .map_err(|error| format!("Failed to read {}: {error}", paths.cache_file.display()))?;
    let catalog = parse_openrouter_catalog(&bytes)?;
    let ollama_cloud = parse_ollama_cloud_catalog(&bytes).unwrap_or_default();
    let lmstudio = parse_lmstudio_catalog(&bytes).unwrap_or_default();
    let deepseek = parse_deepseek_catalog(&bytes).unwrap_or_default();
    let zai = parse_zai_catalog(&bytes).unwrap_or_default();
    let meta = std::fs::read(&paths.meta_file)
        .ok()
        .and_then(|bytes| serde_json::from_slice::<CatalogMeta>(&bytes).ok())
        .unwrap_or_else(|| CatalogMeta {
            source_url: source_url(),
            fetched_at_ms: file_modified_ms(&paths.cache_file).unwrap_or_default(),
            openrouter_model_count: catalog.models.len(),
            ollama_cloud_model_count: ollama_cloud.models.len(),
            lmstudio_model_count: lmstudio.models.len(),
            deepseek_model_count: deepseek.models.len(),
            zai_model_count: zai.models.len(),
        });
    let stale = !is_fresh(&meta, now_ms(), DEFAULT_TTL);
    Ok(CachedOpenRouterCatalog {
        catalog,
        ollama_cloud,
        lmstudio,
        deepseek,
        zai,
        meta,
        stale,
    })
}

fn memory_cache() -> Option<CachedOpenRouterCatalog> {
    MEMORY_CACHE
        .get_or_init(|| RwLock::new(None))
        .read()
        .ok()
        .and_then(|cached| cached.clone())
}

fn store_memory_cache(cached: &CachedOpenRouterCatalog) {
    if let Ok(mut memory) = MEMORY_CACHE.get_or_init(|| RwLock::new(None)).write() {
        *memory = Some(cached.clone());
    }
}

async fn refresh_with_paths(
    force: bool,
    paths: CatalogPaths,
) -> Result<CachedOpenRouterCatalog, String> {
    if disable_fetch() {
        return load_disk_from(&paths).await;
    }

    let lock = REFRESH_LOCK.get_or_init(|| Mutex::new(()));
    let _guard = lock.lock().await;
    if !force {
        if let Ok(cached) = load_disk_from(&paths).await {
            if !cached.stale {
                return Ok(cached);
            }
        }
    }

    let source_url = source_url();
    let bytes = fetch_snapshot(&source_url).await?;
    let catalog = parse_openrouter_catalog(&bytes)?;
    let ollama_cloud = parse_ollama_cloud_catalog(&bytes).unwrap_or_default();
    let lmstudio = parse_lmstudio_catalog(&bytes).unwrap_or_default();
    let deepseek = parse_deepseek_catalog(&bytes).unwrap_or_default();
    let zai = parse_zai_catalog(&bytes).unwrap_or_default();
    let meta = CatalogMeta {
        source_url,
        fetched_at_ms: now_ms(),
        openrouter_model_count: catalog.models.len(),
        ollama_cloud_model_count: ollama_cloud.models.len(),
        lmstudio_model_count: lmstudio.models.len(),
        deepseek_model_count: deepseek.models.len(),
        zai_model_count: zai.models.len(),
    };
    write_validated_snapshot(&paths, &bytes, &meta).await?;
    Ok(CachedOpenRouterCatalog {
        catalog,
        ollama_cloud,
        lmstudio,
        deepseek,
        zai,
        meta,
        stale: false,
    })
}

async fn fetch_snapshot(source_url: &str) -> Result<Vec<u8>, String> {
    let client = Client::builder()
        .timeout(FETCH_TIMEOUT)
        .build()
        .map_err(|error| format!("Failed to create catalog HTTP client: {error}"))?;
    let response = client
        .get(source_url)
        .send()
        .await
        .map_err(|error| format!("Failed to fetch Models.dev catalog: {error}"))?;
    if !response.status().is_success() {
        return Err(format!(
            "Models.dev catalog request failed: {}",
            response.status()
        ));
    }
    if response
        .content_length()
        .is_some_and(|length| length > MAX_RESPONSE_BYTES)
    {
        return Err("Models.dev catalog response was too large.".to_string());
    }
    let bytes = response
        .bytes()
        .await
        .map_err(|error| format!("Failed to read Models.dev catalog response: {error}"))?;
    if bytes.len() as u64 > MAX_RESPONSE_BYTES {
        return Err("Models.dev catalog response was too large.".to_string());
    }
    Ok(bytes.to_vec())
}

async fn write_validated_snapshot(
    paths: &CatalogPaths,
    bytes: &[u8],
    meta: &CatalogMeta,
) -> Result<(), String> {
    parse_openrouter_catalog(bytes)?;
    if let Some(parent) = paths.cache_file.parent() {
        tokio::fs::create_dir_all(parent)
            .await
            .map_err(|error| format!("Failed to create {}: {error}", parent.display()))?;
    }

    let cache_tmp = temp_path(&paths.cache_file);
    let meta_tmp = temp_path(&paths.meta_file);
    let meta_bytes = serde_json::to_vec_pretty(meta)
        .map_err(|error| format!("Failed to serialize catalog metadata: {error}"))?;
    tokio::fs::write(&cache_tmp, bytes)
        .await
        .map_err(|error| format!("Failed to write {}: {error}", cache_tmp.display()))?;
    tokio::fs::write(&meta_tmp, &meta_bytes)
        .await
        .map_err(|error| format!("Failed to write {}: {error}", meta_tmp.display()))?;

    replace_file(&cache_tmp, &paths.cache_file).await?;
    replace_file(&meta_tmp, &paths.meta_file).await?;
    Ok(())
}

async fn read_meta(paths: &CatalogPaths) -> Option<CatalogMeta> {
    let bytes = tokio::fs::read(&paths.meta_file).await.ok()?;
    serde_json::from_slice(&bytes).ok()
}

fn is_fresh(meta: &CatalogMeta, now_ms: u128, ttl: Duration) -> bool {
    now_ms
        .checked_sub(meta.fetched_at_ms)
        .is_some_and(|age| age <= ttl.as_millis())
}

async fn replace_file(from: &Path, to: &Path) -> Result<(), String> {
    if tokio::fs::rename(from, to).await.is_ok() {
        return Ok(());
    }
    let _ = tokio::fs::remove_file(to).await;
    tokio::fs::rename(from, to)
        .await
        .map_err(|error| format!("Failed to replace {}: {error}", to.display()))
}

fn temp_path(path: &Path) -> PathBuf {
    let name = path
        .file_name()
        .and_then(|value| value.to_str())
        .unwrap_or("catalog.tmp");
    path.with_file_name(format!("{name}.{}.tmp", std::process::id()))
}

fn source_url() -> String {
    std::env::var("FENNARA_MODELS_DEV_URL")
        .ok()
        .filter(|url| !url.trim().is_empty())
        .unwrap_or_else(|| DEFAULT_MODELS_DEV_URL.to_string())
}

fn disable_fetch() -> bool {
    std::env::var("FENNARA_DISABLE_MODELS_DEV_FETCH")
        .ok()
        .is_some_and(|value| matches!(value.trim(), "1" | "true" | "TRUE"))
}

fn file_modified_ms(path: &Path) -> Option<u128> {
    std::fs::metadata(path)
        .ok()?
        .modified()
        .ok()?
        .duration_since(UNIX_EPOCH)
        .ok()
        .map(|duration| duration.as_millis())
}

fn now_ms() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture() -> Vec<u8> {
        br#"{
            "openrouter": {
                "id": "openrouter",
                "models": {
                    "ok/model": {
                        "id": "ok/model",
                        "name": "OK Model",
                        "tool_call": true,
                        "temperature": true,
                        "limit": { "context": 100000, "output": 4096 },
                        "modalities": { "input": ["text"], "output": ["text"] }
                    }
                }
            }
        }"#
        .to_vec()
    }

    fn test_paths(name: &str) -> CatalogPaths {
        let base = std::env::temp_dir().join(format!(
            "fennara-catalog-cache-test-{name}-{}",
            std::process::id()
        ));
        let _ = std::fs::remove_dir_all(&base);
        CatalogPaths {
            cache_file: base.join(CACHE_FILE),
            meta_file: base.join(META_FILE),
        }
    }

    #[tokio::test]
    async fn stale_disk_cache_still_loads() {
        let paths = test_paths("stale");
        let meta = CatalogMeta {
            source_url: DEFAULT_MODELS_DEV_URL.to_string(),
            fetched_at_ms: 1,
            openrouter_model_count: 1,
            ollama_cloud_model_count: 0,
            lmstudio_model_count: 0,
            deepseek_model_count: 0,
            zai_model_count: 0,
        };
        write_validated_snapshot(&paths, &fixture(), &meta)
            .await
            .unwrap();

        let loaded = load_disk_from(&paths).await.unwrap();

        assert_eq!(loaded.catalog.models.len(), 1);
        assert!(loaded.stale);
    }

    #[tokio::test]
    async fn malformed_snapshot_does_not_overwrite_cache() {
        let paths = test_paths("invalid");
        let meta = CatalogMeta {
            source_url: DEFAULT_MODELS_DEV_URL.to_string(),
            fetched_at_ms: now_ms(),
            openrouter_model_count: 1,
            ollama_cloud_model_count: 0,
            lmstudio_model_count: 0,
            deepseek_model_count: 0,
            zai_model_count: 0,
        };
        write_validated_snapshot(&paths, &fixture(), &meta)
            .await
            .unwrap();
        let before = tokio::fs::read_to_string(&paths.cache_file).await.unwrap();

        let result = write_validated_snapshot(&paths, b"{ not-json", &meta).await;
        let after = tokio::fs::read_to_string(&paths.cache_file).await.unwrap();

        assert!(result.is_err());
        assert_eq!(before, after);
    }
}
