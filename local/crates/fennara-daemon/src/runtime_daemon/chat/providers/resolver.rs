use super::catalog::{Catalog, model_ref_from_selection};
use super::error::LlmError;
use super::types::{ChatRequest, ProviderSettings, ResolvedModel};

pub(crate) fn resolve_request_model(
    settings: &ProviderSettings,
    request: &ChatRequest,
) -> Result<ResolvedModel, LlmError> {
    let catalog = Catalog::from_settings(settings);
    let model_ref = model_ref_from_selection(&request.model, &catalog)?;
    catalog.resolve(&model_ref)
}
