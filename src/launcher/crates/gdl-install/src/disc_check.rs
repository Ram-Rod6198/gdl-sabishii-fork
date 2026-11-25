//! The check of a GameCube disc's files against fingerprints taken from a trusted disc: for every
//! file of the `Gauntlet` and `carddemo` trees, the SHA-256 digest of its path on the disc beside
//! the SHA-256 digest of its bytes, a set for each revision of the disc. The files are the ones an
//! install copies, so an ISO, a CISO and a scrubbed image of one disc check alike, where a digest
//! of the whole image would tell them apart. The fingerprints hold no name, no size and no byte
//! of the disc, and are read as their writer, scripts/assets/gamecube_fingerprints.py, writes
//! them. The check only reads, and decides nothing: an install holds a disc to its header alone
//! unless whoever runs it asks for the check.

use std::collections::btree_map::Entry;
use std::collections::{BTreeMap, BTreeSet};
use std::io;

use gdl_files::Job;
use gdl_media::{
    DiscEntryKind, GameCubeImage, MAX_DISC_ENTRIES, MAX_REVISION, MediaError, Sha256Digest,
    US_GAME_ID,
};
use serde::Deserialize;

/// The format the fingerprints are written in.
pub const DISC_FINGERPRINTS_FORMAT: &str = "gdl-gamecube-fingerprints";

/// The version of that format the launcher reads.
pub const DISC_FINGERPRINTS_VERSION: u64 = 1;

/// The most files a check names of those that differ: enough to tell a damaged file from another
/// disc, without a report as long as the disc's tree.
pub const MAX_DIFFERING_NAMED: usize = 16;

/// The fingerprints of the US disc's files, as the fingerprint writer took them from the player's
/// own disc on the trusted machine.
const COMMITTED: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/fingerprints/gamecube-us.json"
));

/// The digests a trusted disc's files had, each under the digest of its path on the disc, a set
/// for each revision fingerprints were taken of.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DiscFingerprints {
    /// Each revision's files: a file's digest under its path's digest.
    revisions: BTreeMap<u8, BTreeMap<Sha256Digest, Sha256Digest>>,
}

/// Fingerprints as the fingerprint writer writes them. Keys it does not know are passed over.
#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct Written {
    /// The format they are written in.
    format: String,
    /// The version of that format.
    version: u64,
    /// The game ID of the disc they are for.
    game_id: String,
    /// A set for each revision.
    revisions: Vec<WrittenRevision>,
}

/// One revision's fingerprints as the writer writes them.
#[derive(Deserialize)]
struct WrittenRevision {
    /// The revision the disc's header gives.
    revision: u8,
    /// The fingerprints, in the order of their path digests.
    files: Vec<WrittenRow>,
}

/// A fingerprint as the writer writes one: two digests in lowercase hexadecimal.
#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct WrittenRow {
    /// The digest of the file's path on the disc.
    path_sha256: String,
    /// The digest of the file's bytes.
    sha256: String,
}

impl DiscFingerprints {
    /// The fingerprints `text` holds, read as the fingerprint writer writes them: of
    /// [`DISC_FINGERPRINTS_FORMAT`] at [`DISC_FINGERPRINTS_VERSION`], for the US disc's game ID;
    /// no revision later than [`MAX_REVISION`] and none twice; at most [`MAX_DISC_ENTRIES`] rows a
    /// revision, each two digests of 64 lowercase hexadecimal digits; and no path digest twice in
    /// a revision. Refused by the first of these the text breaks, once it has been read as JSON
    /// of the writer's shape.
    pub fn parse(text: &str) -> Result<DiscFingerprints, DiscFingerprintsError> {
        let written: Written =
            serde_json::from_str(text).map_err(|error| DiscFingerprintsError::Json {
                detail: error.to_string().into(),
            })?;
        if written.format != DISC_FINGERPRINTS_FORMAT {
            return Err(DiscFingerprintsError::Format);
        }
        if written.version != DISC_FINGERPRINTS_VERSION {
            return Err(DiscFingerprintsError::Version);
        }
        if written.game_id.as_bytes() != US_GAME_ID {
            return Err(DiscFingerprintsError::Identity);
        }
        let mut revisions = BTreeMap::new();
        for set in &written.revisions {
            let revision = set.revision;
            if revision > MAX_REVISION {
                return Err(DiscFingerprintsError::Revision { revision });
            }
            if set.files.len() > MAX_DISC_ENTRIES {
                return Err(DiscFingerprintsError::TooMany { revision });
            }
            let mut files = BTreeMap::new();
            for (row, fingerprint) in set.files.iter().enumerate() {
                let digests = Sha256Digest::from_hex(&fingerprint.path_sha256)
                    .zip(Sha256Digest::from_hex(&fingerprint.sha256));
                let (key, digest) =
                    digests.ok_or(DiscFingerprintsError::Digest { revision, row })?;
                if files.insert(key, digest).is_some() {
                    return Err(DiscFingerprintsError::Duplicate {
                        revision,
                        key: key.hex().into(),
                    });
                }
            }
            match revisions.entry(revision) {
                Entry::Vacant(slot) => slot.insert(files),
                Entry::Occupied(_) => {
                    return Err(DiscFingerprintsError::RevisionTwice { revision });
                }
            };
        }
        Ok(DiscFingerprints { revisions })
    }

    /// The fingerprints of the US disc's files, which the launcher carries. Panics only when the
    /// file it carries them in is malformed, which a test rules out.
    pub fn committed() -> DiscFingerprints {
        DiscFingerprints::parse(COMMITTED).expect("the committed fingerprints are well formed")
    }

    /// The fingerprints of one revision: each file of `files` a path on the disc and the digest
    /// of the file at it. A path given twice keeps its last digest.
    pub fn of_revision(
        revision: u8,
        files: impl IntoIterator<Item = (String, Sha256Digest)>,
    ) -> DiscFingerprints {
        let files = files
            .into_iter()
            .map(|(path, digest)| (DiscFingerprints::path_key(&path), digest))
            .collect();
        DiscFingerprints {
            revisions: BTreeMap::from([(revision, files)]),
        }
    }

    /// The key a file's fingerprint is found by: the SHA-256 digest of its path's UTF-8 bytes,
    /// its components joined by `/` and spelled as the disc spells them.
    pub fn path_key(path: &str) -> Sha256Digest {
        Sha256Digest::of(path.as_bytes())
    }

    /// The revisions the fingerprints hold a set for, in their order.
    pub fn revisions(&self) -> impl Iterator<Item = u8> + '_ {
        self.revisions.keys().copied()
    }

    /// How many fingerprints the set of `revision` holds, or `None` when there is no such set.
    pub fn len_of(&self, revision: u8) -> Option<usize> {
        self.revisions.get(&revision).map(BTreeMap::len)
    }
}

/// Why fingerprints were refused.
#[derive(Debug, thiserror::Error)]
pub enum DiscFingerprintsError {
    /// The text is not JSON, or not of the shape fingerprints are written in.
    #[error("the fingerprints are not written as their format writes them: {detail}")]
    Json {
        /// What the JSON reader said.
        detail: Box<str>,
    },
    /// They are written in another format.
    #[error("the fingerprints are not of the format {format}", format = DISC_FINGERPRINTS_FORMAT)]
    Format,
    /// They are written in another version of the format.
    #[error(
        "the fingerprints are not of version {version} of their format",
        version = DISC_FINGERPRINTS_VERSION
    )]
    Version,
    /// They are for another disc than the US one.
    #[error("the fingerprints are for another disc")]
    Identity,
    /// A set is for a revision the US disc was never pressed as.
    #[error("the fingerprints hold a set for revision {revision}, which no US disc is")]
    Revision {
        /// The revision the set gives.
        revision: u8,
    },
    /// Two sets are for one revision.
    #[error("the fingerprints hold two sets for revision {revision}")]
    RevisionTwice {
        /// The revision both sets give.
        revision: u8,
    },
    /// A set holds more fingerprints than a disc's tree holds entries.
    #[error("the fingerprints of revision {revision} are more than a disc holds files")]
    TooMany {
        /// The revision of the set.
        revision: u8,
    },
    /// A row's path digest or file digest is not 64 lowercase hexadecimal digits.
    #[error(
        "fingerprint {row} of revision {revision} does not hold two SHA-256 digests in lowercase \
         hexadecimal"
    )]
    Digest {
        /// The revision of the set.
        revision: u8,
        /// Where the row stands among the set's rows, counted from nought.
        row: usize,
    },
    /// Two fingerprints of a set are kept under one path digest.
    #[error("two fingerprints of revision {revision} are kept under one path digest: {key}")]
    Duplicate {
        /// The revision of the set.
        revision: u8,
        /// The path digest, in lowercase hexadecimal.
        key: Box<str>,
    },
}

/// How a disc's files stand against fingerprints.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DiscCheck {
    /// Every file an install copies holds the bytes the trusted disc's held, and the disc holds
    /// every file the trusted disc did.
    Verified {
        /// The disc's revision.
        revision: u8,
        /// How many files were checked.
        files: usize,
    },
    /// The fingerprints hold no set for the disc's revision, so nothing was read. The disc stands
    /// as its header left it.
    Unlisted {
        /// The disc's revision.
        revision: u8,
    },
    /// The disc is not the trusted one: a file holds other bytes, the disc holds a file the
    /// trusted disc did not, or lacks one it did.
    Differs {
        /// The disc's revision.
        revision: u8,
        /// The first files that hold other bytes or that the trusted disc did not hold, by their
        /// paths on the disc in the file table's order, at most [`MAX_DIFFERING_NAMED`].
        named: Vec<String>,
        /// How many files hold other bytes or were not on the trusted disc.
        differing: usize,
        /// How many files of the trusted disc this one lacks.
        missing: usize,
    },
}

impl DiscCheck {
    /// The check's stable machine name, in camelCase, for reports and logs.
    pub fn as_str(&self) -> &'static str {
        match self {
            DiscCheck::Verified { .. } => "verified",
            DiscCheck::Unlisted { .. } => "unlisted",
            DiscCheck::Differs { .. } => "differs",
        }
    }
}

/// Checks the files of the disc in `image`, every file of the trees an install copies, against
/// the set `fingerprints` holds for the disc's revision. Each file is read whole, in the file
/// table's order, as an install's copy reads it: the job's cancel is looked at before each block
/// and its progress told the bytes of the file done after each. With no set for the revision
/// nothing is read. Fails only when the image cannot be read or the job is asked to stop.
pub fn check_disc(
    image: &mut GameCubeImage,
    fingerprints: &DiscFingerprints,
    job: &Job<'_>,
) -> Result<DiscCheck, MediaError> {
    let revision = image.revision();
    let Some(trusted) = fingerprints.revisions.get(&revision) else {
        return Ok(DiscCheck::Unlisted { revision });
    };
    let entries = image.entries().to_vec();
    let mut found = BTreeSet::new();
    let mut named = Vec::new();
    let mut differing = 0usize;
    let mut files = 0usize;
    for entry in &entries {
        if !matches!(entry.kind, DiscEntryKind::File { .. }) {
            continue;
        }
        files += 1;
        let key = DiscFingerprints::path_key(entry.path.as_str());
        let held = match trusted.get(&key) {
            Some(digest) => {
                found.insert(key);
                image.copy(entry, &mut io::sink(), job)? == *digest
            }
            None => false,
        };
        if !held {
            differing += 1;
            if named.len() < MAX_DIFFERING_NAMED {
                named.push(entry.path.as_str().to_owned());
            }
        }
    }
    let missing = trusted.len() - found.len();
    Ok(if differing == 0 && missing == 0 {
        DiscCheck::Verified { revision, files }
    } else {
        DiscCheck::Differs {
            revision,
            named,
            differing,
            missing,
        }
    })
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::path::Path;

    use gdl_files::{Cancel, Job, NoProgress};
    use gdl_fixtures::{synthetic_ciso, synthetic_iso};
    use gdl_media::{DiscEntryKind, GameCubeImage, MediaError, Sha256Digest};

    use super::{
        DISC_FINGERPRINTS_FORMAT, DiscCheck, DiscFingerprints, DiscFingerprintsError, check_disc,
    };

    /// The image `bytes`, written into `dir` as `name` and opened as the player's image is.
    fn opened(dir: &Path, name: &str, bytes: &[u8]) -> GameCubeImage {
        let path = dir.join(name);
        fs::write(&path, bytes).unwrap();
        GameCubeImage::open(&path).unwrap()
    }

    /// Every file of `image` an install copies: its path on the disc and its digest.
    fn files_of(image: &mut GameCubeImage) -> Vec<(String, Sha256Digest)> {
        let cancel = Cancel::default();
        let job = Job {
            cancel: &cancel,
            progress: &NoProgress,
        };
        let entries = image.entries().to_vec();
        entries
            .iter()
            .filter(|entry| matches!(entry.kind, DiscEntryKind::File { .. }))
            .map(|entry| {
                let digest = image.copy(entry, &mut std::io::sink(), &job).unwrap();
                (entry.path.as_str().to_owned(), digest)
            })
            .collect()
    }

    /// The check of `image` against `fingerprints`, by a job nobody stops.
    fn checked(image: &mut GameCubeImage, fingerprints: &DiscFingerprints) -> DiscCheck {
        let cancel = Cancel::default();
        let job = Job {
            cancel: &cancel,
            progress: &NoProgress,
        };
        check_disc(image, fingerprints, &job).unwrap()
    }

    /// Fingerprints as their writer writes them, with `revisions` as its sets.
    fn written(format: &str, version: u64, game_id: &str, revisions: &str) -> String {
        format!(
            r#"{{"format":"{format}","version":{version},"gameId":"{game_id}","revisions":[{revisions}]}}"#
        )
    }

    #[test]
    fn the_committed_fingerprints_are_well_formed() {
        let committed = DiscFingerprints::committed();
        let revisions: Vec<u8> = committed.revisions().collect();
        assert!(!revisions.is_empty());
        for revision in revisions {
            assert!(
                committed.len_of(revision).unwrap() > 0,
                "revision {revision}"
            );
        }
    }

    #[test]
    fn a_disc_is_verified_as_an_iso_and_as_a_ciso() {
        let dir = tempfile::tempdir().unwrap();
        let iso = synthetic_iso();
        let mut whole = opened(dir.path(), "game.iso", &iso);
        let files = files_of(&mut whole);
        assert!(!files.is_empty());
        let fingerprints = DiscFingerprints::of_revision(whole.revision(), files.clone());
        let verified = DiscCheck::Verified {
            revision: whole.revision(),
            files: files.len(),
        };
        assert_eq!(checked(&mut whole, &fingerprints), verified);
        // The sparse image of the same disc holds the same files, whatever its own bytes are.
        let mut sparse = opened(dir.path(), "game.ciso", &synthetic_ciso(&iso));
        assert_eq!(checked(&mut sparse, &fingerprints), verified);
        assert_eq!(verified.as_str(), "verified");
    }

    #[test]
    fn a_revision_without_fingerprints_is_unlisted_and_unread() {
        let dir = tempfile::tempdir().unwrap();
        let mut image = opened(dir.path(), "game.iso", &synthetic_iso());
        let other = image.revision() ^ 1;
        let fingerprints = DiscFingerprints::of_revision(other, files_of(&mut image));
        // A job already asked to stop shows that no file is read.
        let cancel = Cancel::default();
        cancel.cancel();
        let job = Job {
            cancel: &cancel,
            progress: &NoProgress,
        };
        let check = check_disc(&mut image, &fingerprints, &job).unwrap();
        assert_eq!(
            check,
            DiscCheck::Unlisted {
                revision: image.revision()
            }
        );
        assert_eq!(check.as_str(), "unlisted");
    }

    #[test]
    fn a_changed_an_unknown_and_a_missing_file_each_differ() {
        let dir = tempfile::tempdir().unwrap();
        let mut image = opened(dir.path(), "game.iso", &synthetic_iso());
        let revision = image.revision();
        let files = files_of(&mut image);
        assert!(files.len() >= 2, "the synthetic disc holds several files");

        // Other bytes at a path the trusted disc held.
        let mut changed = files.clone();
        changed[0].1 = Sha256Digest::of(b"other bytes");
        let fingerprints = DiscFingerprints::of_revision(revision, changed);
        assert_eq!(
            checked(&mut image, &fingerprints),
            DiscCheck::Differs {
                revision,
                named: vec![files[0].0.clone()],
                differing: 1,
                missing: 0,
            }
        );

        // A file the trusted disc did not hold.
        let fingerprints = DiscFingerprints::of_revision(revision, files[1..].to_vec());
        assert_eq!(
            checked(&mut image, &fingerprints),
            DiscCheck::Differs {
                revision,
                named: vec![files[0].0.clone()],
                differing: 1,
                missing: 0,
            }
        );

        // A file of the trusted disc that this one lacks.
        let mut more = files.clone();
        more.push(("Gauntlet/absent.bin".to_owned(), Sha256Digest::of(b"")));
        let fingerprints = DiscFingerprints::of_revision(revision, more);
        let check = checked(&mut image, &fingerprints);
        assert_eq!(
            check,
            DiscCheck::Differs {
                revision,
                named: Vec::new(),
                differing: 0,
                missing: 1,
            }
        );
        assert_eq!(check.as_str(), "differs");
    }

    #[test]
    fn a_check_asked_to_stop_is_cancelled() {
        let dir = tempfile::tempdir().unwrap();
        let mut image = opened(dir.path(), "game.iso", &synthetic_iso());
        let fingerprints = DiscFingerprints::of_revision(image.revision(), files_of(&mut image));
        let cancel = Cancel::default();
        cancel.cancel();
        let job = Job {
            cancel: &cancel,
            progress: &NoProgress,
        };
        let error = check_disc(&mut image, &fingerprints, &job).unwrap_err();
        assert!(matches!(error, MediaError::Cancelled), "{error:?}");
    }

    #[test]
    fn written_fingerprints_are_held_to_their_format() {
        let key = Sha256Digest::of(b"Gauntlet/a").hex();
        let digest = Sha256Digest::of(b"a").hex();
        let row = format!(r#"{{"pathSha256":"{key}","sha256":"{digest}"}}"#);
        let set =
            |revision: u8, rows: &str| format!(r#"{{"revision":{revision},"files":[{rows}]}}"#);

        let read = DiscFingerprints::parse(&written(
            DISC_FINGERPRINTS_FORMAT,
            1,
            "GUNE5D",
            &format!("{},{}", set(0, ""), set(1, &row)),
        ))
        .unwrap();
        assert_eq!(read.revisions().collect::<Vec<_>>(), [0, 1]);
        assert_eq!(read.len_of(0), Some(0));
        assert_eq!(read.len_of(1), Some(1));
        assert_eq!(
            read,
            DiscFingerprints {
                revisions: [
                    (0, Default::default()),
                    (
                        1,
                        [(
                            DiscFingerprints::path_key("Gauntlet/a"),
                            Sha256Digest::of(b"a")
                        )]
                        .into()
                    )
                ]
                .into()
            }
        );

        type Refusal = fn(&DiscFingerprintsError) -> bool;
        let upper = row.replace(&digest, &digest.to_uppercase());
        let refused: [(String, Refusal); 8] = [
            ("{".to_owned(), |error| {
                matches!(error, DiscFingerprintsError::Json { .. })
            }),
            (written("other", 1, "GUNE5D", ""), |error| {
                matches!(error, DiscFingerprintsError::Format)
            }),
            (
                written(DISC_FINGERPRINTS_FORMAT, 2, "GUNE5D", ""),
                |error| matches!(error, DiscFingerprintsError::Version),
            ),
            (
                written(DISC_FINGERPRINTS_FORMAT, 1, "GUNP5D", ""),
                |error| matches!(error, DiscFingerprintsError::Identity),
            ),
            (
                written(DISC_FINGERPRINTS_FORMAT, 1, "GUNE5D", &set(2, "")),
                |error| matches!(error, DiscFingerprintsError::Revision { revision: 2 }),
            ),
            (
                written(
                    DISC_FINGERPRINTS_FORMAT,
                    1,
                    "GUNE5D",
                    &format!("{},{}", set(1, ""), set(1, "")),
                ),
                |error| matches!(error, DiscFingerprintsError::RevisionTwice { revision: 1 }),
            ),
            (
                written(DISC_FINGERPRINTS_FORMAT, 1, "GUNE5D", &set(1, &upper)),
                |error| {
                    matches!(
                        error,
                        DiscFingerprintsError::Digest {
                            revision: 1,
                            row: 0
                        }
                    )
                },
            ),
            (
                written(
                    DISC_FINGERPRINTS_FORMAT,
                    1,
                    "GUNE5D",
                    &set(1, &format!("{row},{row}")),
                ),
                |error| matches!(error, DiscFingerprintsError::Duplicate { revision: 1, .. }),
            ),
        ];
        for (text, is_the_refusal) in refused {
            let error = DiscFingerprints::parse(&text).unwrap_err();
            assert!(is_the_refusal(&error), "{text}: {error}");
        }
    }
}
