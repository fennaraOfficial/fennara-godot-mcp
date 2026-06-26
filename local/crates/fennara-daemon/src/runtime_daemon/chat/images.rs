use base64::{Engine, engine::general_purpose::STANDARD};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};

pub(crate) const MAX_IMAGE_COUNT: usize = 4;
pub(crate) const MAX_IMAGE_BYTES: usize = 3 * 1024 * 1024;
pub(crate) const MAX_TOTAL_IMAGE_BYTES: usize = 20 * 1024 * 1024;

#[derive(Clone, Debug, Deserialize)]
pub(crate) struct ClientImage {
    pub(crate) base64: String,
    pub(crate) mime_type: Option<String>,
    pub(crate) description: Option<String>,
    pub(crate) name: Option<String>,
    pub(crate) size: Option<usize>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
pub(crate) struct ChatImage {
    pub(crate) base64: String,
    pub(crate) mime_type: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) description: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) name: Option<String>,
    pub(crate) size_bytes: usize,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct ImagePlaceholder {
    pub(crate) mime_type: String,
    pub(crate) name: Option<String>,
}

pub(crate) fn validate_images(images: Option<Vec<ClientImage>>) -> Result<Vec<ChatImage>, String> {
    let Some(images) = images else {
        return Ok(Vec::new());
    };
    if images.len() > MAX_IMAGE_COUNT {
        return Err(format!("Attach up to {MAX_IMAGE_COUNT} images."));
    }

    let mut validated = Vec::with_capacity(images.len());
    let mut total_bytes = 0usize;
    for image in images {
        let validated_image = validate_image(image)?;
        total_bytes = total_bytes.saturating_add(validated_image.size_bytes);
        if total_bytes > MAX_TOTAL_IMAGE_BYTES {
            return Err(format!(
                "Attached images must be {} MB total or less.",
                MAX_TOTAL_IMAGE_BYTES / 1024 / 1024
            ));
        }
        validated.push(validated_image);
    }
    Ok(validated)
}

pub(crate) fn user_content_value(text: &str, images: &[ChatImage]) -> Value {
    if images.is_empty() {
        return json!(text);
    }
    let mut parts = Vec::new();
    if !text.trim().is_empty() {
        parts.push(json!({ "type": "text", "text": text }));
    }
    for image in images {
        parts.push(image_content_part(image));
    }
    Value::Array(parts)
}

pub(crate) fn user_content_with_image_placeholders(
    text: &str,
    images: &[ImagePlaceholder],
) -> Value {
    if images.is_empty() {
        return json!(text);
    }
    let mut parts = Vec::new();
    if !text.trim().is_empty() {
        parts.push(json!({ "type": "text", "text": text }));
    }
    for image in images {
        let filename = image.name.as_deref().unwrap_or("file");
        parts.push(json!({
            "type": "text",
            "text": format!("[Attached {}: {}]", image.mime_type, filename)
        }));
    }
    Value::Array(parts)
}

pub(crate) fn metadata_value(images: &[ChatImage]) -> Option<Value> {
    if images.is_empty() {
        None
    } else {
        Some(json!({ "images": images }))
    }
}

fn validate_image(image: ClientImage) -> Result<ChatImage, String> {
    let (data_url_mime, base64) = split_data_url(&image.base64)?;
    let declared_mime = image
        .mime_type
        .as_deref()
        .map(|mime| normalize_mime(Some(mime)))
        .transpose()?;
    let data_mime = data_url_mime
        .as_deref()
        .map(|mime| normalize_mime(Some(mime)))
        .transpose()?;
    if declared_mime.is_some() && data_mime.is_some() && declared_mime != data_mime {
        return Err("Image type does not match the data URL.".to_string());
    }
    let mime_type = declared_mime
        .or(data_mime)
        .ok_or_else(|| "Image type is required.".to_string())?;
    if !base64.chars().all(is_base64_char) {
        return Err("Image payload is not valid base64.".to_string());
    }
    let approx_bytes = base64.len().saturating_mul(3) / 4;
    if approx_bytes > MAX_IMAGE_BYTES + 2 {
        return Err(format!(
            "Each image must be {} MB or smaller.",
            MAX_IMAGE_BYTES / 1024 / 1024
        ));
    }

    let decoded = STANDARD
        .decode(base64.as_bytes())
        .map_err(|_| "Image payload is not valid base64.".to_string())?;
    if decoded.is_empty() {
        return Err("Image payload is empty.".to_string());
    }
    if decoded.len() > MAX_IMAGE_BYTES || image.size.is_some_and(|size| size > MAX_IMAGE_BYTES) {
        return Err(format!(
            "Each image must be {} MB or smaller.",
            MAX_IMAGE_BYTES / 1024 / 1024
        ));
    }

    let detected_mime = detect_mime(&decoded)
        .ok_or_else(|| "Unsupported image file. Use PNG, JPEG, WebP, or GIF.".to_string())?;
    if detected_mime != mime_type {
        return Err("Image type does not match the file contents.".to_string());
    }

    Ok(ChatImage {
        base64,
        mime_type,
        description: clean_short(image.description),
        name: clean_short(image.name),
        size_bytes: decoded.len(),
    })
}

fn split_data_url(raw: &str) -> Result<(Option<String>, String), String> {
    let trimmed = raw.trim();
    if !trimmed.starts_with("data:") {
        return Ok((None, trimmed.to_string()));
    }
    let Some((prefix, payload)) = trimmed.split_once(',') else {
        return Err("Image data URL is malformed.".to_string());
    };
    let mime = prefix
        .strip_prefix("data:")
        .and_then(|value| value.split(';').next())
        .map(str::to_string);
    if !prefix.to_ascii_lowercase().contains(";base64") {
        return Err("Image data URL must use base64 encoding.".to_string());
    }
    Ok((mime, payload.to_string()))
}

fn normalize_mime(mime: Option<&str>) -> Result<String, String> {
    let clean = mime.unwrap_or("").trim().to_ascii_lowercase();
    match clean.as_str() {
        "image/png" | "image/jpeg" | "image/webp" | "image/gif" => Ok(clean),
        "image/jpg" => Ok("image/jpeg".to_string()),
        "" => Err("Image type is required.".to_string()),
        "image/svg+xml" => Err("SVG images are not supported.".to_string()),
        _ => Err("Unsupported image type. Use PNG, JPEG, WebP, or GIF.".to_string()),
    }
}

fn detect_mime(bytes: &[u8]) -> Option<String> {
    if bytes.starts_with(b"\x89PNG\r\n\x1a\n") {
        return Some("image/png".to_string());
    }
    if bytes.starts_with(b"\xff\xd8\xff") {
        return Some("image/jpeg".to_string());
    }
    if bytes.len() >= 12 && bytes.starts_with(b"RIFF") && &bytes[8..12] == b"WEBP" {
        return Some("image/webp".to_string());
    }
    if bytes.starts_with(b"GIF87a") || bytes.starts_with(b"GIF89a") {
        return Some("image/gif".to_string());
    }
    None
}

fn image_content_part(image: &ChatImage) -> Value {
    json!({
        "type": "image_url",
        "image_url": {
            "url": format!("data:{};base64,{}", image.mime_type, image.base64)
        }
    })
}

fn clean_short(value: Option<String>) -> Option<String> {
    value
        .map(|value| value.trim().chars().take(120).collect::<String>())
        .filter(|value| !value.is_empty())
}

fn is_base64_char(ch: char) -> bool {
    ch.is_ascii_alphanumeric() || matches!(ch, '+' | '/' | '=')
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn historical_image_placeholders_do_not_include_base64() {
        let content = user_content_with_image_placeholders(
            "look at this",
            &[ImagePlaceholder {
                mime_type: "image/png".to_string(),
                name: Some("screenshot.png".to_string()),
            }],
        );

        assert_eq!(
            content,
            json!([
                { "type": "text", "text": "look at this" },
                { "type": "text", "text": "[Attached image/png: screenshot.png]" }
            ])
        );
        assert!(!content.to_string().contains("base64"));
    }

    #[test]
    fn current_images_still_build_image_url_parts() {
        let content = user_content_value(
            "current",
            &[ChatImage {
                base64: "aGVsbG8=".to_string(),
                mime_type: "image/png".to_string(),
                description: None,
                name: Some("current.png".to_string()),
                size_bytes: 5,
            }],
        );

        assert!(content.to_string().contains("image_url"));
        assert!(
            content
                .to_string()
                .contains("data:image/png;base64,aGVsbG8=")
        );
    }
}
