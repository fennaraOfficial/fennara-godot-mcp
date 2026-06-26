#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct SseParts {
    pub(crate) events: Vec<String>,
    pub(crate) rest: String,
}

pub(crate) fn parse_sse_payloads(buffer: &str) -> SseParts {
    let normalized = buffer.replace("\r\n", "\n").replace('\r', "\n");
    let parts: Vec<&str> = normalized.split("\n\n").collect();
    let rest = parts.last().copied().unwrap_or_default().to_string();
    let events = parts
        .iter()
        .take(parts.len().saturating_sub(1))
        .map(|part| part.to_string())
        .collect();
    SseParts { events, rest }
}

pub(crate) fn data_lines(event: &str) -> Vec<String> {
    event
        .lines()
        .map(str::trim)
        .filter_map(|line| line.strip_prefix("data:"))
        .map(str::trim)
        .map(ToString::to_string)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keeps_partial_event_in_rest() {
        let parsed = parse_sse_payloads("data: one\n\ndata: two");

        assert_eq!(parsed.events, vec!["data: one"]);
        assert_eq!(parsed.rest, "data: two");
    }

    #[test]
    fn extracts_multiple_data_lines() {
        assert_eq!(
            data_lines("event: message\ndata: one\ndata: two"),
            vec!["one", "two"]
        );
    }
}
